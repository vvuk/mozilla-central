/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#ifdef MOZ_VALGRIND
# include <valgrind/helgrind.h>
# include <valgrind/memcheck.h>
#endif

// mmap
#include <sys/mman.h>

// Next 4 needed to keep TableTicker.h happy
#include "sps_sampler.h"
#include "platform.h"
#include "shared-libraries.h"
#include <iostream>

#include "ProfileEntry2.h"
#include "TableTicker2.h"

// Also defines the SPS_PLAT_ macros
#include "UnwinderThread2.h"

#if defined(SPS_PLAT_arm_android)
# include "android-signal-defs.h"
#endif

/* FIXME -- HACK */
#undef NDEBUG
#include <assert.h>


/* Verbosity of this module, for debugging:
     0  silent
     1  adds info about debuginfo load success/failure
     2  adds slow-summary stats for buffer fills/misses (RECOMMENDED)
     3  adds per-sample summary lines
     4  adds per-sample frame listing
   Note that level 3 and above produces risk of deadlock, and 
   are not recommended for extended use.
*/
#define LOGLEVEL 2


//////////////////////////////////////////////////////////
//// BEGIN externally visible functions

// fwdses
// the unwinder thread ID, its fn, and a stop-now flag
static void* unwind_thr_fn ( void* exit_nowV );
static pthread_t unwind_thr;
static int       unwind_thr_exit_now = 0; // RACED ON

// Threads must be registered with this file before they can be
// sampled.  So that we know the max safe stack address for each
// registered thread.
static void thread_register_for_profiling ( void* stackTop );

// RUNS IN SIGHANDLER CONTEXT
// Acquire an empty buffer and mark it as FILLING
static UnwinderThreadBuffer* acquire_empty_buffer();

// RUNS IN SIGHANDLER CONTEXT
// Put this buffer in the queue of stuff going to the unwinder
// thread, and mark it as FULL.  Before doing that, fill in stack
// chunk and register fields if a native unwind is requested.
// APROFILE is where the profile data should be added to.  UTB
// is the partially-filled-in buffer, containing ProfileEntries.
// UCV is the ucontext_t* from the signal handler.  If non-NULL, is
// taken as a cue to request native unwind.
static void release_full_buffer(ThreadProfile* aProfile,
                                UnwinderThreadBuffer* utb,
                                void* /* ucontext_t*, really */ ucV );

// RUNS IN SIGHANDLER CONTEXT
static void utb_add_profent(UnwinderThreadBuffer* utb, ProfileEntry ent);

// Do a store fence.
static void do_SFENCE ( void );


void uwt__init()
{
  // Create the unwinder thread.
  assert(unwind_thr_exit_now == 0);
  int r = pthread_create( &unwind_thr, NULL,
                          unwind_thr_fn, (void*)&unwind_thr_exit_now );
  assert(r==0);
}

void uwt__deinit()
{
  // Shut down the unwinder thread.
  assert(unwind_thr_exit_now == 0);
  unwind_thr_exit_now = 1;
  do_SFENCE();
  int r = pthread_join(unwind_thr, NULL); assert(r==0);
}

void uwt__register_thread_for_profiling ( void* stackTop )
{
  thread_register_for_profiling(stackTop);
}

// RUNS IN SIGHANDLER CONTEXT
UnwinderThreadBuffer* uwt__acquire_empty_buffer()
{
  return acquire_empty_buffer();
}

// RUNS IN SIGHANDLER CONTEXT
void
uwt__release_full_buffer(ThreadProfile* aProfile,
                         UnwinderThreadBuffer* utb,
                         void* /* ucontext_t*, really */ ucV )
{
  release_full_buffer( aProfile, utb, ucV );
}

// RUNS IN SIGHANDLER CONTEXT
void
utb__addEntry(/*MOD*/UnwinderThreadBuffer* utb, ProfileEntry ent)
{
  utb_add_profent(utb, ent);
}

//// END externally visible functions
//////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////
//// BEGIN type UnwindThreadBuffer

typedef  unsigned int            UInt;  /* always 32-bit */
typedef  unsigned long int       UWord; /* machine word */
typedef  unsigned long long int  ULong; /* always 64-bit */

typedef
   struct { 
      ULong rsp;
      ULong rbp;
      ULong rip; 
   }
   AMD64Regs;

typedef
   struct {
      UInt r15;
      UInt r14;
      UInt r13;
      UInt r12;
      UInt r11;
      UInt r7;
   }
   ARMRegs;

#if defined(SPS_PLAT_amd64_linux)
typedef  AMD64Regs  ArchRegs;
#elif defined(SPS_PLAT_arm_android)
typedef  ARMRegs  ArchRegs;
#else
#  error "Unknown plat"
#endif

#if defined(SPS_PLAT_amd64_linux)
# define SPS_PAGE_SIZE 4096
#elif defined(SPS_PLAT_arm_android)
# define SPS_PAGE_SIZE 4096
#else
#  error "Unknown plat"
#endif

typedef  enum { S_EMPTY, S_FILLING, S_EMPTYING, S_FULL }  State;

typedef  struct { UWord val; }  SpinLock;

/* CONFIGURABLE */
/* The maximum number of bytes in a stack snapshot */
#define N_STACK_BYTES 32768

/* CONFIGURABLE */
/* The number of fixed ProfileEntry slots.  If more are required, they
   are placed in mmap'd pages. */
#define N_FIXED_PROF_ENTS 20

/* CONFIGURABLE */
/* The number of extra pages of ProfileEntries.  If (on arm) each
   ProfileEntry is 8 bytes, then a page holds 512, and so 100 pages
   is enough to hold 51200. */
#define N_PROF_ENT_PAGES 100

/* DERIVATIVE */
#define N_PROF_ENTS_PER_PAGE (SPS_PAGE_SIZE / sizeof(ProfileEntry))

/* A page of ProfileEntrys.  This might actually be slightly smaller
   than a page if SPS_PAGE_SIZE is not an exact multiple of
   sizeof(ProfileEntry). */
typedef
  struct { ProfileEntry ents[N_PROF_ENTS_PER_PAGE]; }
  ProfEntsPage;

#define ProfEntsPage_INVALID ((ProfEntsPage*)1)


/* Fields protected by the spinlock are marked SL */

struct _UnwinderThreadBuffer {
  /*SL*/ State  state;
  /* The rest of these are protected, in some sense, by ::state.  If
     ::state is S_FILLING, they are 'owned' by the sampler thread
     that set the state to S_FILLING.  If ::state is S_EMPTYING,
     they are 'owned' by the unwinder thread that set the state to
     S_EMPTYING.  If ::state is S_EMPTY or S_FULL, the buffer isn't
     owned by any thread, and so no thread may access these
     fields. */
  /* Sample number, needed to process samples in order */
  ULong          seqNo;
  /* The ThreadProfile into which the results are eventually to be
     dumped. */
  ThreadProfile* aProfile;
  /* Pseudostack and other info, always present */
  ProfileEntry   entsFixed[N_FIXED_PROF_ENTS];
  ProfEntsPage*  entsPages[N_PROF_ENT_PAGES];
  UWord          entsUsed;
  /* Do we also have data to do a native unwind? */
  bool           haveNativeInfo;
  /* If so, here is the register state and stack.  Unset if
     .haveNativeInfo is false. */
  ArchRegs       regs;
  unsigned char  stackImg[N_STACK_BYTES];
  int            stackImgUsed;
  void*          stackImgAddr; /* VMA corresponding to stackImg[0] */
  void*          stackMaxSafe; /* VMA for max safe stack reading */
};
/* Indexing scheme for ents:
     0 <= i < N_FIXED_PROF_ENTS
       is at entsFixed[i]

     i >= N_FIXED_PROF_ENTS
       is at let j = i - N_FIXED_PROF_ENTS
             in  entsPages[j / N_PROFENTS_PER_PAGE]
                  ->ents[j % N_PROFENTS_PER_PAGE]
     
   entsPages[] are allocated on demand.  Because zero can
   theoretically be a valid page pointer, use 
   ProfEntsPage_INVALID == (ProfEntsPage*)1 to mark invalid pages.

   It follows that the max entsUsed value is N_FIXED_PROF_ENTS +
   N_PROFENTS_PER_PAGE * N_PROFENTS_PAGES, and at that point no more
   ProfileEntries can be storedd.
*/


typedef
  struct {
    pthread_t thrId;
    void*     stackTop;
    ULong     nSamples; 
  }
  StackLimit;

/* Globals -- the buffer array */
#define N_UNW_THR_BUFFERS 10
/*SL*/ static UnwinderThreadBuffer** g_buffers     = NULL;
/*SL*/ static ULong                  g_seqNo       = 0;
/*SL*/ static SpinLock               g_spinLock    = { 0 };

/* Globals -- the thread array */
#define N_SAMPLING_THREADS 10
/*SL*/ static StackLimit g_stackLimits[N_SAMPLING_THREADS];
/*SL*/ static int        g_stackLimitsUsed = 0;

/* Stats -- atomically incremented, no lock needed */
static UWord g_stats_totalSamples = 0; // total # sample attempts
static UWord g_stats_noBuffAvail  = 0; // # failed due to no buffer avail

/* We must be VERY CAREFUL what we do with the spinlock held.  The
   only thing it is safe to do with it held is modify (viz, read or
   write) g_buffers, g_buffers[], g_seqNo, g_buffers[]->state,
   g_stackLimits[] and g_stackLimitsUsed.  No arbitrary computations,
   no syscalls, no printfs, no file IO, and absolutely no dynamic
   memory allocation (else we WILL eventually deadlock).

   This applies both to the signal handler and to the unwinder thread.
*/

//// END type UnwindThreadBuffer
//////////////////////////////////////////////////////////

// fwds
// the interface to breakpad
typedef  struct { u_int64_t pc; u_int64_t sp; }  PCandSP;

static
void do_breakpad_unwind_Buffer ( /*OUT*/PCandSP** pairs,
                                 /*OUT*/unsigned int* nPairs,
                                 UnwinderThreadBuffer* buff,
                                 int buffNo /* for debug printing only */ );

static bool is_page_aligned(void* v)
{
  UWord w = (UWord) v;
  return (w & (SPS_PAGE_SIZE-1)) == 0  ? true  : false;
}


/* Implement machine-word sized atomic compare-and-swap. */
/* return 1 if success, 0 if failure */
static int do_CASW ( UWord* addr, UWord expected, UWord nyu )
{
#if defined(SPS_PLAT_amd64_linux)
   UWord block[4] = { (UWord)addr, expected, nyu, 2 };
   __asm__ __volatile__(
      "movq 0(%%rsi),  %%rdi"         "\n\t" // addr
      "movq 8(%%rsi),  %%rax"         "\n\t" // expected
      "movq 16(%%rsi), %%rbx"         "\n\t" // nyu
      "xorq %%rcx,%%rcx"              "\n\t"
      "lock; cmpxchgq %%rbx,(%%rdi)"  "\n\t"
      "setz %%cl"                     "\n\t"
      "movq %%rcx, 24(%%rsi)"         "\n"
      : /*out*/ 
      : /*in*/ "S"(&block[0])
      : /*trash*/"memory","cc","rdi","rax","rbx","rcx"
   );
   assert(block[3] == 0 || block[3] == 1);
   return (int)(block[3] & 1);
#elif defined(SPS_PLAT_arm_android)
   while (1) {
      UWord old, success;
      UWord block[2] = { (UWord)addr, nyu };
      /* Fetch the old value, and set the reservation */
      __asm__ __volatile__ (
         "ldrex  %0, [%1]"    "\n"
         : /*out*/   "=r"(old)
         : /*in*/    "r"(addr)
      );
      /* If the old value isn't as expected, we've had it */
      if (old != expected) return 0;
      /* Try to store the new value. */
      __asm__ __volatile__(
         "ldr    r4, [%1, #0]"      "\n\t"
         "ldr    r5, [%1, #4]"      "\n\t"
         "strex  r6, r5, [r4, #0]"  "\n\t"
         "eor    %0, r6, #1"        "\n\t"
         : /*out*/ "=r"(success)
         : /*in*/ "r"(&block[0])
         : /*trash*/ "r4","r5","r6","memory"
      );
      assert(success == 0 || success == 1);
      if (success == 1) return 1;
      /* Although the old value was as we expected, we failed to store
         the new value, presumably because we were out-raced by some
         other thread, or the hardware invalidated the reservation for
         some other reason.  Either way, we have to start over. */
   }
#else
#  error "Undefined plat"
#endif
}

/* Hint to the CPU core that we are in a spin-wait loop, and that
   other processors/cores/threads-running-on-the-same-core should be
   given priority on execute resources, if that is possible. */
static void do_PAUSE ( void )
{
#if defined(SPS_PLAT_amd64_linux)
  __asm__ __volatile__("rep; nop"); // F3 90 -- check this
#elif defined(SPS_PLAT_arm_android)
  /* Some variant of WFE here, but be careful .. will need SEV to wake
     up afterwards. */
#else
#  error "Undefined plat"
#endif
}

/* Perform a load fence, that is: loads which occur later in program
   order than the fence may not be reordered (by the CPU) to occur
   before the fence.  FIXME: add a more correct description. */
static void do_LFENCE ( void )
{
#if defined(SPS_PLAT_amd64_linux)
   __asm__ __volatile__("lfence");
#elif defined(SPS_PLAT_arm_android)
   __asm__ __volatile__("dsb sy; dmb sy; isb");
#else
#  error "Undefined plat"
#endif
}

/* Perform a store fence, that is: stores that have been done by this
   processor prior to this instruction must be made visible to other
   processors before this instruction finishes.  In particular, stores
   by this processor cannot be deferred (for some definition of
   deferred) past this point. */
static void do_SFENCE ( void )
{
#if defined(SPS_PLAT_amd64_linux)
   __asm__ __volatile__("sfence");
#elif defined(SPS_PLAT_arm_android)
   __asm__ __volatile__("dsb sy; dmb sy; isb");
#else
#  error "Undefined plat"
#endif
}

static void spinLock_acquire ( SpinLock* sl )
{
   UWord* val = &sl->val;
   //VALGRIND_HG_MUTEX_LOCK_PRE(sl, 0/*!isTryLock*/);
   while (1) {
      int ok = do_CASW( val, 0, 1 );
      if (ok) break;
      do_PAUSE();
   }
   do_LFENCE();
   //VALGRIND_HG_MUTEX_LOCK_POST(sl);
}

static void spinLock_release ( SpinLock* sl )
{
   UWord* val = &sl->val;
   //VALGRIND_HG_MUTEX_UNLOCK_PRE(sl);
   do_SFENCE();
   int ok = do_CASW( val, 1, 0 );
   /* This must succeed at the first try.  To fail would imply that
      the lock was unheld. */
   assert(ok);
   //VALGRIND_HG_MUTEX_UNLOCK_POST(sl);
}

static void sleep_ms ( unsigned int ms )
{
   struct timespec req;
   req.tv_sec = ((time_t)ms) / 1000;
   req.tv_nsec = 1000 * 1000 * (((unsigned long)ms) % 1000);
   nanosleep(&req, NULL);
}

/* Use CAS to implement standalone atomic increment. */
static void atomic_INC ( UWord* loc )
{
  while (1) {
    UWord old = *loc;
    UWord nyu = old + 1;
    int ok = do_CASW( loc, old, nyu );
    if (ok) break;
  }
}

/* Register a thread for profiling.  It must not be allowed to receive
   signals before this is done, else the signal handler will
   assert. */
static void thread_register_for_profiling ( void* stackTop )
{
   int i;
   /* Minimal sanity check on stackTop */
   assert( (void*)&i < stackTop );

   spinLock_acquire(&g_spinLock);

   pthread_t me = pthread_self();
   for (i = 0; i < g_stackLimitsUsed; i++) {
      /* check for duplicate registration */
      assert(g_stackLimits[i].thrId != me);
   }
   assert(g_stackLimitsUsed < N_SAMPLING_THREADS);
   g_stackLimits[g_stackLimitsUsed].thrId    = me;
   g_stackLimits[g_stackLimitsUsed].stackTop = stackTop;
   g_stackLimits[g_stackLimitsUsed].nSamples = 0;
   g_stackLimitsUsed++;

   spinLock_release(&g_spinLock);
}


__attribute__((unused))
static void show_registered_threads ( void )
{
   int i;
   spinLock_acquire(&g_spinLock);
   for (i = 0; i < g_stackLimitsUsed; i++) {
     LOGF("[%d]  pthread_t=%p  nSamples=%lld",
          i, (void*)g_stackLimits[i].thrId, g_stackLimits[i].nSamples);
   }
   spinLock_release(&g_spinLock);
}


// RUNS IN SIGHANDLER CONTEXT
static UnwinderThreadBuffer* acquire_empty_buffer()
{
  /* acq lock
     if buffers == NULL { rel lock; exit }
     scan to find a free buff; if none { rel lock; exit }
     set buff state to S_FILLING
     fillseqno++; and remember it
     rel lock
  */
  int i;

  atomic_INC( &g_stats_totalSamples );

  /* This code is critical.  We are in a signal handler and possibly
     with the malloc lock held.  So we can't allocate any heap, and
     can't safely call any C library functions, not even the pthread_
     functions.  And we certainly can't do any syscalls.  In short,
     this function needs to be self contained, not do any allocation,
     and not hold on to the spinlock for any significant length of
     time. */

  spinLock_acquire(&g_spinLock);

  /* First of all, look for this thread's entry in g_stackLimits[].
     We need to find it in order to figure out how much stack we can
     safely copy into the sample.  This assumes that pthread_self()
     is safe to call in a signal handler, which strikes me as highly
     likely. */
  pthread_t me = pthread_self();
  assert(g_stackLimitsUsed >= 0 && g_stackLimitsUsed <= N_SAMPLING_THREADS);
  for (i = 0; i < g_stackLimitsUsed; i++) {
    if (g_stackLimits[i].thrId == me)
      break;
  }
  /* "this thread is registered for profiling" */
  assert(i < g_stackLimitsUsed);

  /* The furthest point that we can safely scan back up the stack. */
  void* myStackTop = g_stackLimits[i].stackTop;
  g_stackLimits[i].nSamples++;

  /* Try to find a free buffer to use. */
  if (g_buffers == NULL) {
    /* The unwinder thread hasn't allocated any buffers yet.
       Nothing we can do. */
    spinLock_release(&g_spinLock);
    atomic_INC( &g_stats_noBuffAvail );
    return NULL;
  }

  for (i = 0; i < N_UNW_THR_BUFFERS; i++) {
    if (g_buffers[i]->state == S_EMPTY)
      break;
  }
  assert(i >= 0 && i <= N_UNW_THR_BUFFERS);

  if (i == N_UNW_THR_BUFFERS) {
    /* Again, no free buffers .. give up. */
    spinLock_release(&g_spinLock);
    atomic_INC( &g_stats_noBuffAvail );
    if (LOGLEVEL >= 3)
      LOG("BPUnw: handler:  no free buffers");
    return NULL;
  }

  /* So we can use this one safely.  Whilst still holding the lock,
     mark the buffer as belonging to us, and increment the sequence
     number. */
  UnwinderThreadBuffer* buff = g_buffers[i];
  assert(buff->state == S_EMPTY);
  buff->state = S_FILLING;
  buff->seqNo = g_seqNo;
  g_seqNo++;

  if (0)
    LOGF("BPUnw: handler:  seqNo %llu: filling  buf %d",
         g_seqNo-1, i);

  /* And drop the lock.  We own the buffer, so go on and fill it. */
  spinLock_release(&g_spinLock);

  /* Now we own the buffer, initialise it. */
  buff->aProfile       = NULL;
  buff->entsUsed       = 0;
  buff->haveNativeInfo = false;
  buff->stackImgUsed   = 0;
  buff->stackImgAddr   = 0;
  buff->stackMaxSafe   = myStackTop; /* We will need this in
                                        release_full_buffer() */
  for (i = 0; i < N_PROF_ENT_PAGES; i++)
    buff->entsPages[i] = ProfEntsPage_INVALID;
  return buff;
}


// RUNS IN SIGHANDLER CONTEXT
/* The calling thread owns the buffer, as denoted by its state being
   S_FILLING.  So we can mess with it without further locking. */
static void release_full_buffer(ThreadProfile* aProfile,
                                UnwinderThreadBuffer* buff,
                                void* /* ucontext_t*, really */ ucV )
{
  assert(buff->state == S_FILLING);

  ////////////////////////////////////////////////////
  // BEGIN fill

  /* The buffer already will have some of its ProfileEntries filled
     in, but everything else needs to be filled in at this point. */
  //LOGF("Release full buffer: %lu ents", buff->entsUsed);
  /* Where the resulting info is to be dumped */
  buff->aProfile = aProfile;

  /* And, if we have register state, that and the stack top */
  buff->haveNativeInfo = ucV != NULL;
  if (buff->haveNativeInfo) {
#   if defined(SPS_PLAT_amd64_linux)
    ucontext_t* uc = (ucontext_t*)ucV;
    mcontext_t* mc = &(uc->uc_mcontext);
    buff->regs.rip = mc->gregs[REG_RIP];
    buff->regs.rsp = mc->gregs[REG_RSP];
    buff->regs.rbp = mc->gregs[REG_RBP];
#   elif defined(SPS_PLAT_arm_android)
    ucontext_t* uc = (ucontext_t*)ucV;
    mcontext_t* mc = &(uc->uc_mcontext);
    buff->regs.r15 = mc->arm_pc; //gregs[R15];
    buff->regs.r14 = mc->arm_lr; //gregs[R14];
    buff->regs.r13 = mc->arm_sp; //gregs[R13];
    buff->regs.r12 = mc->arm_ip; //gregs[R12];
    buff->regs.r11 = mc->arm_fp; //gregs[R11];
    buff->regs.r7  = mc->arm_r7; //gregs[R7];
#   else
#     error "Unknown plat"
#   endif

    //VALGRIND_PRINTF_BACKTRACE("UNWIND AT\n");
    /* Copy up to N_STACK_BYTES from rsp-REDZONE upwards, but not
       going past the stack's registered top point.  Do some basic
       sanity checks too. */
    { 
#     if defined(SPS_PLAT_amd64_linux)
      UWord rEDZONE_SIZE = 128;
      UWord start = buff->regs.rsp - rEDZONE_SIZE;
#     elif defined(SPS_PLAT_arm_android)
      UWord rEDZONE_SIZE = 0;
      UWord start = buff->regs.r13 - rEDZONE_SIZE;
#     else
#       error "Unknown plat"
#     endif
      UWord end   = (UWord)buff->stackMaxSafe;
      UWord ws    = sizeof(void*);
      start &= ~(ws-1);
      end   &= ~(ws-1);
      UWord nToCopy = 0;
      if (start < end) {
        nToCopy = end - start;
        if (nToCopy > N_STACK_BYTES)
          nToCopy = N_STACK_BYTES;
      }
      assert(nToCopy <= N_STACK_BYTES);
      buff->stackImgUsed = nToCopy;
      buff->stackImgAddr = (void*)start;
      if (0) LOGF("BPUnw nToCopy %lu", nToCopy);
      if (nToCopy > 0) {
        memcpy(&buff->stackImg[0], (void*)start, nToCopy);
        //(void)VALGRIND_MAKE_MEM_DEFINED(&buff->stackImg[0], nToCopy);
      }
    }
  } /* if (buff->haveNativeInfo) */
  // END fill
  ////////////////////////////////////////////////////

  /* And now relinquish ownership of the buff, so that an unwinder
     thread can pick it up. */
  spinLock_acquire(&g_spinLock);
  buff->state = S_FULL;
  spinLock_release(&g_spinLock);
}


// RUNS IN SIGHANDLER CONTEXT
// Allocate a ProfEntsPage, without using malloc, or return
// ProfEntsPage_INVALID if we can't for some reason.
static ProfEntsPage* mmap_anon_ProfEntsPage()
{
  void* v = ::mmap(NULL, sizeof(ProfEntsPage), PROT_READ|PROT_WRITE, 
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (v == MAP_FAILED) {
    return ProfEntsPage_INVALID;
  } else {
    //LOGF("mmap-> %p", v);
    return (ProfEntsPage*)v;
  }
}

// Runs in the unwinder thread
// Free a ProfEntsPage as allocated by mmap_anon_ProfEntsPage
static void munmap_ProfEntsPage(ProfEntsPage* pep)
{
  assert(is_page_aligned(pep));
  ::munmap(pep, sizeof(ProfEntsPage));
  //LOGF("munmap %p", pep);
}


// RUNS IN SIGHANDLER CONTEXT
void
utb_add_profent(/*MOD*/UnwinderThreadBuffer* utb, ProfileEntry ent)
{
  UWord limit
    = N_FIXED_PROF_ENTS + (N_PROF_ENTS_PER_PAGE * N_PROF_ENT_PAGES);
  if (utb->entsUsed == limit) {
    /* We're full.  Now what? */
    LOG("BPUnw: utb__addEntry: NO SPACE for ProfileEntry; ignoring.");
    return;
  }
  assert(utb->entsUsed < limit);

  /* Will it fit in the fixed array? */
  if (utb->entsUsed < N_FIXED_PROF_ENTS) {
    utb->entsFixed[utb->entsUsed] = ent;
    utb->entsUsed++;
    return;
  }

  /* No.  Put it in the extras. */
  UWord i     = utb->entsUsed;
  UWord j     = i - N_FIXED_PROF_ENTS;
  UWord j_div = j / N_PROF_ENTS_PER_PAGE; /* page number */
  UWord j_mod = j % N_PROF_ENTS_PER_PAGE; /* page offset */
  ProfEntsPage* pep = utb->entsPages[j_div];
  if (pep == ProfEntsPage_INVALID) {
    pep = mmap_anon_ProfEntsPage();
    if (pep == ProfEntsPage_INVALID) {
      /* Urr, we ran out of memory.  Now what? */
      LOG("BPUnw: utb__addEntry: MMAP FAILED for ProfileEntry; ignoring.");
      return;
    }
    utb->entsPages[j_div] = pep;
  }
  pep->ents[j_mod] = ent;
  utb->entsUsed++;
}


// misc helper
static ProfileEntry utb_get_profent(UnwinderThreadBuffer* buff, UWord i)
{
  assert(i < buff->entsUsed);
  if (i < N_FIXED_PROF_ENTS) {
    return buff->entsFixed[i];
  } else {
    UWord j     = i - N_FIXED_PROF_ENTS;
    UWord j_div = j / N_PROF_ENTS_PER_PAGE; /* page number */
    UWord j_mod = j % N_PROF_ENTS_PER_PAGE; /* page offset */
    assert(buff->entsPages[j_div] != ProfEntsPage_INVALID);
    return buff->entsPages[j_div]->ents[j_mod];
  }
}


// Runs in the unwinder thread -- well, this _is_ the unwinder thread.
static void* unwind_thr_fn ( void* exit_nowV )
{
  /* If we're the first thread in, we'll need to allocate the buffer
     array g_buffers plus the Buffer structs that it points at. */
  spinLock_acquire(&g_spinLock);
  if (g_buffers == NULL) {
    /* Drop the lock, make a complete copy in memory, reacquire the
       lock, and try to install it -- which might fail, if someone
       else beat us to it. */
    spinLock_release(&g_spinLock);
    UnwinderThreadBuffer** buffers
      = (UnwinderThreadBuffer**)malloc(N_UNW_THR_BUFFERS
                                        * sizeof(UnwinderThreadBuffer*));
    assert(buffers);
    int i;
    for (i = 0; i < N_UNW_THR_BUFFERS; i++) {
      buffers[i] = (UnwinderThreadBuffer*)
                   calloc(sizeof(UnwinderThreadBuffer), 1);
      assert(buffers[i]);
      buffers[i]->state = S_EMPTY;
    }
    /* Try to install it */
    spinLock_acquire(&g_spinLock);
    if (g_buffers == NULL) {
      g_buffers = buffers;
      spinLock_release(&g_spinLock);
    } else {
      /* Someone else beat us to it.  Release what we just allocated
         so as to avoid a leak. */
      spinLock_release(&g_spinLock);
      for (i = 0; i < N_UNW_THR_BUFFERS; i++) {
        free(buffers[i]);
      }
      free(buffers);
    }
  } else {
    /* They are already allocated, so just drop the lock and continue. */
    spinLock_release(&g_spinLock);
  }

  /* 
    while (1) {
      acq lock
      scan to find oldest full
         if none { rel lock; sleep; continue }
      set buff state to emptying
      rel lock
      acq MLock // implicitly
      process buffer
      rel MLock // implicitly
      acq lock
      set buff state to S_EMPTY
      rel lock
    }
  */
  int* exit_now = (int*)exit_nowV;
  int ms_to_sleep_if_empty = 1;
  while (1) {

    if (*exit_now != 0) break;

    spinLock_acquire(&g_spinLock);

    /* Find the oldest filled buffer, if any. */
    ULong   oldest_seqNo = ~0ULL; /* infinity */
    int     oldest_ix    = -1;
    int     i;
    for (i = 0; i < N_UNW_THR_BUFFERS; i++) {
      UnwinderThreadBuffer* buff = g_buffers[i];
      if (buff->state != S_FULL) continue;
      if (buff->seqNo < oldest_seqNo) {
        oldest_seqNo = buff->seqNo;
        oldest_ix    = i;
      }
    }
    if (oldest_ix == -1) {
      /* We didn't find a full buffer.  Snooze and try again later. */
      assert(oldest_seqNo == ~0ULL);
      spinLock_release(&g_spinLock);
      if (ms_to_sleep_if_empty > 100 && LOGLEVEL >= 2) {
        LOGF("BPUnw: unwinder: sleep for %d ms", ms_to_sleep_if_empty);
      }
      sleep_ms(ms_to_sleep_if_empty);
      if (ms_to_sleep_if_empty < 20) {
        ms_to_sleep_if_empty += 2;
      } else {
        ms_to_sleep_if_empty = (15 * ms_to_sleep_if_empty) / 10;
        if (ms_to_sleep_if_empty > 1000)
          ms_to_sleep_if_empty = 1000;
      }
      continue;
    }

    /* We found a full a buffer.  Mark it as 'ours' and drop the
       lock; then we can safely throw breakpad at it. */
    UnwinderThreadBuffer* buff = g_buffers[oldest_ix];
    assert(buff->state == S_FULL);
    buff->state = S_EMPTYING;
    spinLock_release(&g_spinLock);

    /* unwind .. in which we can do anything we like, since any
       resource stalls that we may encounter (eg malloc locks) in
       competition with signal handler instances, will be short
       lived since the signal handler is guaranteed nonblocking. */
    if (0) LOGF("BPUnw: unwinder: seqNo %llu: emptying buf %d\n",
                oldest_seqNo, oldest_ix);

    /* Copy ProfileEntries presented to us by the sampling thread.
       Most of them are copied verbatim into |buff->aProfile|,
       except for 'hint' tags, which direct us to do something
       different. */

    /* Need to lock |aProfile| so nobody tries to copy out entries
       whilst we are putting them in. */
    buff->aProfile->GetMutex()->Lock();

    /* The buff is a sequence of ProfileEntries (ents).  It has
       this grammar:

       | --pre-tags-- | (h 'P' .. h 'Q')* | --post-tags-- |
                        ^               ^
                        ix_first_hP     ix_last_hQ

       Each (h 'P' .. h 'Q') subsequence represents one pseudostack
       entry.  These, if present, are in the order
       outermost-frame-first, and that is the order that they should
       be copied into aProfile.  The --pre-tags-- and --post-tags--
       are to be copied into the aProfile verbatim, except that they
       may contain the hints "h 'F'" for a flush and "h 'N'" to
       indicate that a native unwind is also required, and must be
       interleaved with the pseudostack entries.

       The hint tags that bound each pseudostack entry, "h 'P'" and "h
       'Q'", are not to be copied into the aProfile -- they are
       present only to make parsing easy here.  Also, the pseudostack
       entries may contain an "'S' (void*)" entry, which is the stack
       pointer value for that entry, and these are also not to be
       copied.
    */
    /* The first thing to do is therefore to find the pseudostack
       entries, if any, and to find out also whether a native unwind
       has been requested. */
    const UWord infUW = ~(UWord)0; // infinity
    bool  need_native_unw = false;
    UWord ix_first_hP    = infUW; // "not found"
    UWord ix_last_hQ     = infUW; // "not found"

    UWord k;
    for (k = 0; k < buff->entsUsed; k++) {
      ProfileEntry ent = utb_get_profent(buff, k);
      if (ent.is_ent_hint('N')) {
        need_native_unw = true;
      }
      else if (ent.is_ent_hint('P') && ix_first_hP == ~(UWord)0) {
        ix_first_hP = k;
      }
      else if (ent.is_ent_hint('Q')) {
        ix_last_hQ = k;
      }
    }

    if (0) LOGF("BPUnw: ix_first_hP %lu  ix_last_hQ %lu  need_native_unw %lu",
                ix_first_hP, ix_last_hQ, (UWord)need_native_unw);

    /* There are four possibilities: native-only, pseudostack-only,
       combined (both), and neither.  We handle all four cases. */

    assert( (ix_first_hP == infUW && ix_last_hQ == infUW) ||
            (ix_first_hP != infUW && ix_last_hQ != infUW) );
    bool have_P = ix_first_hP != infUW;
    if (have_P) {
      assert(ix_first_hP < ix_last_hQ);
      assert(ix_last_hQ <= buff->entsUsed);
    }

    /* Neither N nor P.  This is very unusual but has been observed to happen.
       Just copy to the output. */
    if (!need_native_unw && !have_P) {
      for (k = 0; k < buff->entsUsed; k++) {
        ProfileEntry ent = utb_get_profent(buff, k);
        // action flush-hints
        if (ent.is_ent_hint('F')) { buff->aProfile->flush(); continue; }
        // skip ones we can't copy
        if (ent.is_ent_hint() || ent.is_ent('S')) { continue; }
        // and copy everything else
        buff->aProfile->addTag( ent );
      }
    }
    else /* Native only-case. */
    if (need_native_unw && !have_P) {
      for (k = 0; k < buff->entsUsed; k++) {
        ProfileEntry ent = utb_get_profent(buff, k);
        // action a native-unwind-now hint
        if (ent.is_ent_hint('N')) {
          assert(buff->haveNativeInfo);
          PCandSP* pairs = NULL;
          unsigned int nPairs = 0;
          do_breakpad_unwind_Buffer(&pairs, &nPairs, buff, oldest_ix);
          buff->aProfile->addTag( ProfileEntry('s', "(root)") );
          for (unsigned int i = 0; i < nPairs; i++) {
            buff->aProfile
                ->addTag( ProfileEntry('l', reinterpret_cast<void*>(pairs[i].pc)) );
          }
          if (pairs)
            free(pairs);
          continue;
        }
        // action flush-hints
        if (ent.is_ent_hint('F')) { buff->aProfile->flush(); continue; }
        // skip ones we can't copy
        if (ent.is_ent_hint() || ent.is_ent('S')) { continue; }
        // and copy everything else
        buff->aProfile->addTag( ent );
      }
    }
    else /* Pseudostack-only case */
    if (!need_native_unw && have_P) {
      /* If there's no request for a native stack, it's easy: just
         copy the tags verbatim into aProfile, skipping the ones that
         can't be copied -- 'h' (hint) tags, and "'S' (void*)"
         stack-pointer tags.  Except, insert a sample-start tag when
         we see the start of the first pseudostack frame. */
      for (k = 0; k < buff->entsUsed; k++) {
        ProfileEntry ent = utb_get_profent(buff, k);
        // We need to insert a sample-start tag before the first frame
        if (k == ix_first_hP) {
          buff->aProfile->addTag( ProfileEntry('s', "(root)") );
        }
        // action flush-hints
        if (ent.is_ent_hint('F')) { buff->aProfile->flush(); continue; }
        // skip ones we can't copy
        if (ent.is_ent_hint() || ent.is_ent('S')) { continue; }
        // and copy everything else
        buff->aProfile->addTag( ent );
      }
    }
    else /* Combined case */
    if (need_native_unw && have_P)
    {
      /* We need to get a native stacktrace and merge it with the
         pseudostack entries.  This isn't too simple.  First, copy all
         the tags up to the start of the pseudostack tags.  Then
         generate a combined set of tags by native unwind and
         pseudostack.  Then, copy all the stuff after the pseudostack
         tags. */
      assert(buff->haveNativeInfo);

      // Get native unwind info
      PCandSP* pairs = NULL;
      unsigned int n_pairs = 0;
      do_breakpad_unwind_Buffer(&pairs, &n_pairs, buff, oldest_ix);

      // Entries before the pseudostack frames
      for (k = 0; k < ix_first_hP; k++) {
        ProfileEntry ent = utb_get_profent(buff, k);
        // action flush-hints
        if (ent.is_ent_hint('F')) { buff->aProfile->flush(); continue; }
        // skip ones we can't copy
        if (ent.is_ent_hint() || ent.is_ent('S')) { continue; }
        // and copy everything else
        buff->aProfile->addTag( ent );
      }

      // BEGIN merge
      buff->aProfile->addTag( ProfileEntry('s', "(root)") );
      unsigned int next_N = 0; // index in pairs[]
      unsigned int next_P = ix_first_hP; // index in buff profent array
      bool last_was_P = false;
      if (0) LOGF("at mergeloop: n_pairs %lu ix_last_hQ %lu",
                  (UWord)n_pairs, (UWord)ix_last_hQ);
      while (true) {
        if (next_P <= ix_last_hQ) {
          // Assert that next_P points at the start of an P entry
          assert(utb_get_profent(buff, next_P).is_ent_hint('P'));
        }
        if (next_N >= n_pairs && next_P > ix_last_hQ) {
          // both stacks empty
          break;
        }
        /* Decide which entry to use next:
           If N is empty, must use P, and vice versa
           else
           If the last was P and current P has zero SP, use P
           else
           we assume that both P and N have valid SP, in which case
              use the one with the larger value
        */
        bool use_P = true;
        if (next_N >= n_pairs) {
          // N empty, use P
          use_P = true;
          if (0) LOG("  P  <=  no remaining N entries");
        }
        else if (next_P > ix_last_hQ) {
          // P empty, use N
          use_P = false;
          if (0) LOG("  N  <=  no remaining P entries");
        }
        else {
          // We have at least one N and one P entry available.
          // Scan forwards to find the SP of the current P entry
          u_int64_t sp_cur_P = 0;
          unsigned int m = next_P + 1;
          while (1) {
            /* This assertion should hold because in a well formed
               input, we must eventually find the hint-Q that marks
               the end of this frame's entries. */
            assert(m < buff->entsUsed);
            ProfileEntry ent = utb_get_profent(buff, m);
            if (ent.is_ent_hint('Q'))
              break;
            if (ent.is_ent('S')) {
              sp_cur_P = reinterpret_cast<u_int64_t>(ent.get_tagPtr());
              break;
            }
            m++;
          }
          if (last_was_P && sp_cur_P == 0) {
            if (0) LOG("  P  <=  last_was_P && sp_cur_P == 0");
            use_P = true;
          } else {
            u_int64_t sp_cur_N = pairs[next_N].sp;
            use_P = (sp_cur_P > sp_cur_N);
            if (0) LOGF("  %s  <=  sps P %p N %p",
                        use_P ? "P" : "N", (void*)(intptr_t)sp_cur_P, 
                                           (void*)(intptr_t)sp_cur_N);
          }
        }
        /* So, we know which we are going to use. */
        if (use_P) {
          unsigned int m = next_P + 1;
          while (true) {
            assert(m < buff->entsUsed);
            ProfileEntry ent = utb_get_profent(buff, m);
            if (ent.is_ent_hint('Q')) {
              next_P = m + 1;
              break;
            }
            // we don't expect a flush-hint here
            assert(!ent.is_ent_hint('F'));
            // skip ones we can't copy
            if (ent.is_ent_hint() || ent.is_ent('S')) { m++; continue; }
            // and copy everything else
            buff->aProfile->addTag( ent );
            m++;
          }
        } else {
          buff->aProfile
              ->addTag( ProfileEntry('l', reinterpret_cast<void*>(pairs[next_N].pc)) );
          next_N++;
        }
        /* Remember what we chose, for next time. */
        last_was_P = use_P;
      }

      assert(next_P == ix_last_hQ + 1);
      assert(next_N == n_pairs);
      // END merge

      // Entries after the pseudostack frames
      for (k = ix_last_hQ+1; k < buff->entsUsed; k++) {
        ProfileEntry ent = utb_get_profent(buff, k);
        // action flush-hints
        if (ent.is_ent_hint('F')) { buff->aProfile->flush(); continue; }
        // skip ones we can't copy
        if (ent.is_ent_hint() || ent.is_ent('S')) { continue; }
        // and copy everything else
        buff->aProfile->addTag( ent );
      }

      // free native unwind info
      if (pairs)
        free(pairs);
    }

#if 0
    bool show = true;
    if (show) LOG("----------------");
    for (k = 0; k < buff->entsUsed; k++) {
      ProfileEntry ent = utb_get_profent(buff, k);
      if (show) ent.log();
      if (ent.is_ent_hint('F')) {
        /* This is a flush-hint */
        buff->aProfile->flush();
      } 
      else if (ent.is_ent_hint('N')) {
        /* This is a do-a-native-unwind-right-now hint */
        assert(buff->haveNativeInfo);
        PCandSP* pairs = NULL;
        unsigned int nPairs = 0;
        do_breakpad_unwind_Buffer(&pairs, &nPairs, buff, oldest_ix);
        buff->aProfile->addTag( ProfileEntry('s', "(root)") );
        for (unsigned int i = 0; i < nPairs; i++) {
          buff->aProfile
              ->addTag( ProfileEntry('l', reinterpret_cast<void*>(pairs[i].pc)) );
        }
        if (pairs)
          free(pairs);
      } else {
        /* Copy in verbatim */
        buff->aProfile->addTag( ent );
      }
    }
#endif

    buff->aProfile->GetMutex()->Unlock();

    /* And .. we're done.  Mark the buffer as empty so it can be
       reused.  First though, unmap any of the entsPages that got
       mapped during filling. */
    for (i = 0; i < N_PROF_ENT_PAGES; i++) {
      if (buff->entsPages[i] == ProfEntsPage_INVALID)
        continue;
      munmap_ProfEntsPage(buff->entsPages[i]);
      buff->entsPages[i] = ProfEntsPage_INVALID;
    }

    //(void)VALGRIND_MAKE_MEM_UNDEFINED(&buff->stackImg[0], N_STACK_BYTES);
    spinLock_acquire(&g_spinLock);
    assert(buff->state == S_EMPTYING);
    buff->state = S_EMPTY;
    spinLock_release(&g_spinLock);
    ms_to_sleep_if_empty = 1;
  }
  return NULL;
}


////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

/* After this point, we have some classes that interface with
   breakpad, that allow us to pass in a Buffer and get an unwind of
   it. */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "google_breakpad/common/minidump_format.h"
#include "google_breakpad/processor/call_stack.h"
#include "google_breakpad/processor/stack_frame_cpu.h"
#include "local_debug_info_symbolizer.h"
#include "processor/stackwalker_amd64.h"
#include "processor/stackwalker_arm.h"
#include "processor/logging.h"
#include "common/linux/dump_symbols.h"

#include "google_breakpad/processor/memory_region.h"
#include "google_breakpad/processor/code_modules.h"

google_breakpad::MemoryRegion* foo = NULL;

/* FIXME -- HACK */
#undef NDEBUG
#include <assert.h>

using std::string;

///////////////////////////////////////////////////////////////////
/* Implement MemoryRegion, so that it hauls stack image data out of
   the stack top snapshots that the signal handler has so carefully
   snarfed. */

// BEGIN: DERIVED FROM src/processor/stackwalker_selftest.cc
//
class BufferMemoryRegion : public google_breakpad::MemoryRegion {
 public:
  // We just keep hold of the Buffer* we're given, but make no attempt
  // to take allocation-ownership of it.
  BufferMemoryRegion(UnwinderThreadBuffer* buff) : buff_(buff) { }
  ~BufferMemoryRegion() { }

  u_int64_t GetBase() const { return (UWord)buff_->stackImgAddr; }
  u_int32_t GetSize() const { return (UWord)buff_->stackImgUsed; }

  bool GetMemoryAtAddress(u_int64_t address, u_int8_t*  value) const {
      return GetMemoryAtAddressInternal(address, value); }
  bool GetMemoryAtAddress(u_int64_t address, u_int16_t* value) const {
      return GetMemoryAtAddressInternal(address, value); }
  bool GetMemoryAtAddress(u_int64_t address, u_int32_t* value) const {
      return GetMemoryAtAddressInternal(address, value); }
  bool GetMemoryAtAddress(u_int64_t address, u_int64_t* value) const {
      return GetMemoryAtAddressInternal(address, value); }

 private:
  template<typename T> bool GetMemoryAtAddressInternal (
                               u_int64_t address, T* value) const {
    /* Range check .. */
    if ( ((UWord)address) >= ((UWord)buff_->stackImgAddr)
         && (((UWord)address) + sizeof(T) - 1)
            <= (((UWord)buff_->stackImgAddr) + buff_->stackImgUsed - 1)) {
      UWord offset = (UWord)address - (UWord)buff_->stackImgAddr;
      if (0) LOGF("GMAA %lx ok", (UWord)address);
      *value = *reinterpret_cast<const T*>(&buff_->stackImg[offset]);
      return true;
    } else {
      if (0) LOGF("GMAA %lx failed", (UWord)address);
      return false;
    }
  }

  // where this all comes from
  UnwinderThreadBuffer* buff_;
};
//
// END: DERIVED FROM src/processor/stackwalker_selftest.cc


///////////////////////////////////////////////////////////////////
/* Implement MyCodeModule and MyCodeModules, so they pull the relevant
   information about which modules are loaded where out of
   /proc/self/maps. */

class MyCodeModule : public google_breakpad::CodeModule {
public:
   MyCodeModule(u_int64_t x_start, u_int64_t x_len,
                string filename, u_int64_t offset)
     : x_start_(x_start), x_len_(x_len),
       filename_(filename), offset_(offset) {
     assert(x_len > 0);
  }

  ~MyCodeModule() {}

  // The base address of this code module as it was loaded by the process.
  // (u_int64_t)-1 on error.
  u_int64_t base_address() const { return x_start_; }

  // The size of the code module.  0 on error.
  u_int64_t size() const { return x_len_; }

  // The path or file name that the code module was loaded from.  Empty on
  // error.
  string code_file() const { return filename_; }

  // An identifying string used to discriminate between multiple versions and
  // builds of the same code module.  This may contain a uuid, timestamp,
  // version number, or any combination of this or other information, in an
  // implementation-defined format.  Empty on error.
  string code_identifier() const { assert(0); return ""; }

  // The filename containing debugging information associated with the code
  // module.  If debugging information is stored in a file separate from the
  // code module itself (as is the case when .pdb or .dSYM files are used),
  // this will be different from code_file.  If debugging information is
  // stored in the code module itself (possibly prior to stripping), this
  // will be the same as code_file.  Empty on error.
  string debug_file() const { assert(0); return ""; }

  // An identifying string similar to code_identifier, but identifies a
  // specific version and build of the associated debug file.  This may be
  // the same as code_identifier when the debug_file and code_file are
  // identical or when the same identifier is used to identify distinct
  // debug and code files.
  string debug_identifier() const { assert(0); return ""; }

  // A human-readable representation of the code module's version.  Empty on
  // error.
  string version() const { assert(0); return ""; }

  // Creates a new copy of this CodeModule object, which the caller takes
  // ownership of.  The new CodeModule may be of a different concrete class
  // than the CodeModule being copied, but will behave identically to the
  // copied CodeModule as far as the CodeModule interface is concerned.
  const CodeModule* Copy() const { assert(0); return NULL; }

 private:
    // record info for a file backed executable mapping
    // snarfed from /proc/self/maps
    u_int64_t x_start_;
    u_int64_t x_len_;    // may not be zero
    string    filename_; // of the mapped file
    u_int64_t offset_;   // in the mapped file
};


class MyCodeModules : public google_breakpad::CodeModules
{
 public:
   MyCodeModules() {
      // read /proc/self/maps and create a vector of CodeModule*
      assert(mods.size() == 0);
      FILE* f = fopen("/proc/self/maps", "r");
      assert(f);
      while (!feof(f)) {
         unsigned long long int start = 0;
         unsigned long long int end   = 0;
         char rr = ' ', ww = ' ', xx = ' ', pp = ' ';
         unsigned long long int offset = 0, inode = 0;
         unsigned int devMaj = 0, devMin = 0;
         int nItems = fscanf(f, "%llx-%llx %c%c%c%c %llx %x:%x %llu",
                             &start, &end, &rr, &ww, &xx, &pp,
                             &offset, &devMaj, &devMin, &inode);
         if (nItems == EOF && feof(f)) break;
         assert(nItems == 10);
         assert(start < end);
         // read the associated file name, if it is present
         int ch;
         // find '/' or EOL
         while (1) {
            ch = fgetc(f);
            assert(ch != EOF);
            if (ch == '\n' || ch == '/') break;
         }
         string fname("");
         if (ch == '/') {
            fname += (char)ch;
            while (1) {
               ch = fgetc(f);
               assert(ch != EOF);
               if (ch == '\n') break;
               fname += (char)ch;
            }
         }
         assert(ch == '\n');
         if (0) LOGF("SEG %llx %llx %c %c %c %c %s",
                     start, end, rr, ww, xx, pp, fname.c_str() );
         if (xx == 'x' && fname != "") {
            MyCodeModule* cm = new MyCodeModule( start, end-start,
                                                 fname, offset  );
            mods.push_back(cm);
         }
      }
      fclose(f);
      if (0) printf("got %d mappings\n", (int)mods.size());
   }

   ~MyCodeModules() {
      std::vector<MyCodeModule*>::const_iterator it;
      for (it = mods.begin(); it < mods.end(); it++) {
         MyCodeModule* cm = *it;
         delete cm;
      }
   }

 private:
   std::vector<MyCodeModule*> mods;

   unsigned int module_count() const { assert(0); return 1; }

   const google_breakpad::CodeModule*
                 GetModuleForAddress(u_int64_t address) const {
      if (0) printf("GMFA %lx\n", (UWord)address);
      std::vector<MyCodeModule*>::const_iterator it;
      for (it = mods.begin(); it < mods.end(); it++) {
         MyCodeModule* cm = *it;
         if (0) printf("considering %p  %llx +%llx\n",
                       (void*)cm, (ULong)cm->base_address(), (ULong)cm->size());
         if (cm->base_address() <= address
             && address < cm->base_address() + cm->size())
            return cm;
      }
      return NULL;
   }

   const google_breakpad::CodeModule* GetMainModule() const {
      assert(0); return NULL; return NULL;
   }

   const google_breakpad::CodeModule* GetModuleAtSequence(
                 unsigned int sequence) const {
      assert(0); return NULL;
   }

   const google_breakpad::CodeModule* GetModuleAtIndex
                 (unsigned int index) const {
      assert(0); return NULL;
   }

   const CodeModules* Copy() const {
      assert(0); return NULL;
   }
};

///////////////////////////////////////////////////////////////////
/* Top level interface to breakpad.  Given a Buffer* as carefully
   acquired by the signal handler and later handed to this thread,
   unwind it.

   The first time in, read /proc/self/maps.  TODO: what about if it
   changes as we go along?

   Dump the result (PC, SP) pairs in a malloc-allocated array of
   PCandSPs, and return that and its length to the caller.  Caller is
   responsible for deallocating it.

   The first pair is for the outermost frame, the last for the
   innermost frame.
*/

MyCodeModules*    sModules  = NULL;
google_breakpad::LocalDebugInfoSymbolizer* sSymbolizer = NULL;

void do_breakpad_unwind_Buffer ( /*OUT*/PCandSP** pairs,
                                 /*OUT*/unsigned int* nPairs,
                                 UnwinderThreadBuffer* buff,
                                 int buffNo /* for debug printing only */ )
{
# if defined(SPS_PLAT_amd64_linux)
  MDRawContextAMD64* context = new MDRawContextAMD64();
  memset(context, 0, sizeof(*context));

  context->rip = buff->regs.rip;
  context->rbp = buff->regs.rbp;
  context->rsp = buff->regs.rsp;

  if (0) {
    LOGF("Initial RIP = 0x%lx", context->rip);
    LOGF("Initial RSP = 0x%lx", context->rsp);
    LOGF("Initial RBP = 0x%lx", context->rbp);
  }

# elif defined(SPS_PLAT_arm_android)
  MDRawContextARM* context = new MDRawContextARM();
  memset(context, 0, sizeof(*context));

  context->iregs[7]                     = buff->regs.r7;
  context->iregs[12]                    = buff->regs.r12;
  context->iregs[MD_CONTEXT_ARM_REG_PC] = buff->regs.r15;
  context->iregs[MD_CONTEXT_ARM_REG_LR] = buff->regs.r14;
  context->iregs[MD_CONTEXT_ARM_REG_SP] = buff->regs.r13;
  context->iregs[MD_CONTEXT_ARM_REG_FP] = buff->regs.r11;

  if (0) {
    LOGF("Initial R15 = 0x%x",
         context->iregs[MD_CONTEXT_ARM_REG_PC]);
    LOGF("Initial R13 = 0x%x",
         context->iregs[MD_CONTEXT_ARM_REG_SP]);
  }

# else
#   error "Unknown plat"
# endif

  BufferMemoryRegion* memory = new BufferMemoryRegion(buff);

  if (!sModules) {
     sModules = new MyCodeModules();
  }

  if (!sSymbolizer) {
     /* Make up a list of places where the debug objects might be. */
     std::vector<std::string> debug_dirs;
#    if defined(SPS_PLAT_amd64_linux)
     debug_dirs.push_back("/usr/lib/debug/lib");
     debug_dirs.push_back("/usr/lib/debug/usr/lib");
     debug_dirs.push_back("/usr/lib/debug/lib/x86_64-linux-gnu");
     debug_dirs.push_back("/usr/lib/debug/usr/lib/x86_64-linux-gnu");
#    elif defined(SPS_PLAT_arm_android)
     debug_dirs.push_back("/sdcard/symbols/system/lib");
     debug_dirs.push_back("/sdcard/symbols/system/bin");
#    else
#      error "Unknown plat"
#    endif
    sSymbolizer = new google_breakpad::LocalDebugInfoSymbolizer(debug_dirs);
  }

# if defined(SPS_PLAT_amd64_linux)
  google_breakpad::StackwalkerAMD64* sw
   = new google_breakpad::StackwalkerAMD64(NULL, context,
                                           memory, sModules,
                                           sSymbolizer);
# elif defined(SPS_PLAT_arm_android)
  google_breakpad::StackwalkerARM* sw
   = new google_breakpad::StackwalkerARM(NULL, context,
                                         -1/*FP reg*/,
                                         memory, sModules,
                                         sSymbolizer);
# else
#   error "Unknown plat"
# endif

  google_breakpad::CallStack* stack = new google_breakpad::CallStack();

  bool b = sw->Walk(stack);
  (void)b;

  unsigned int n_frames = stack->frames()->size();
  unsigned int n_frames_good = 0;

  *pairs  = (PCandSP*)malloc(n_frames * sizeof(PCandSP));
  *nPairs = n_frames;
  if (*pairs == NULL) {
    *nPairs = 0;
    return;
  }

  if (n_frames > 0) {
    //buff->aProfile->addTag(ProfileEntry('s', "(root)"));
    for (unsigned int frame_index = 0; 
         frame_index < n_frames; ++frame_index) {
      google_breakpad::StackFrame *frame = stack->frames()->at(frame_index);

      if (frame->trust == google_breakpad::StackFrame::FRAME_TRUST_CFI
          || frame->trust == google_breakpad::StackFrame::FRAME_TRUST_CONTEXT) {
        n_frames_good++;
      }

#     if defined(SPS_PLAT_amd64_linux)
      google_breakpad::StackFrameAMD64* frame_amd64
        = reinterpret_cast<google_breakpad::StackFrameAMD64*>(frame);
      if (LOGLEVEL >= 4) {
        LOGF("frame %d   rip=0x%016lx rsp=0x%016lx    %s", 
             frame_index,
             frame_amd64->context.rip, frame_amd64->context.rsp, 
             frame_amd64->trust_description().c_str());
      }
      (*pairs)[n_frames-1-frame_index].pc = frame_amd64->context.rip;
      (*pairs)[n_frames-1-frame_index].sp = frame_amd64->context.rsp;

#     elif defined(SPS_PLAT_arm_android)
      google_breakpad::StackFrameARM* frame_arm
        = reinterpret_cast<google_breakpad::StackFrameARM*>(frame);
      if (LOGLEVEL >= 4) {
        LOGF("frame %d   0x%08x   %s",
             frame_index,
             frame_arm->context.iregs[MD_CONTEXT_ARM_REG_PC],
             frame_arm->trust_description().c_str());
      }
      (*pairs)[n_frames-1-frame_index].pc
        = frame_arm->context.iregs[MD_CONTEXT_ARM_REG_PC];
      (*pairs)[n_frames-1-frame_index].sp
        = frame_arm->context.iregs[MD_CONTEXT_ARM_REG_SP];

#     else
#       error "Unknown plat"
#     endif
    }
  }

  if (LOGLEVEL >= 3) {
    LOGF("BPUnw: unwinder: seqNo %llu, buf %d: got %u frames "
         "(%u trustworthy)", 
         buff->seqNo, buffNo, n_frames, n_frames_good);
  }

  if (LOGLEVEL >= 2) {
    if (0 == (g_stats_totalSamples % 1000))
      LOGF("BPUnw: %lu total samples, %lu failed due to buffer unavail",
           g_stats_totalSamples, g_stats_noBuffAvail);
  }

  delete stack;
  delete sw;
  //delete modules;
  delete memory;
  delete context;
}
