/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#include "nsRenderingContextPh.h"
#include "nsRegionPh.h"
#include <math.h>
#include "libimg.h"
#include "nsDeviceContextPh.h"
#include "prprf.h"
#include "nsDrawingSurfacePh.h"
#include "nsGfxCIID.h"
#include "nsGraphicsStatePh.h"

#include <stdlib.h>
#include <mem.h>
#include <photon/PhRender.h>
#include <Pt.h>


static NS_DEFINE_IID(kIRenderingContextIID, NS_IRENDERING_CONTEXT_IID);
static NS_DEFINE_IID(kIDrawingSurfaceIID, NS_IDRAWING_SURFACE_IID);
static NS_DEFINE_IID(kDrawingSurfaceCID, NS_DRAWING_SURFACE_CID);
static NS_DEFINE_CID(kRegionCID, NS_REGION_CID);


#define FLAG_CLIP_VALID       0x0001
#define FLAG_CLIP_CHANGED     0x0002
#define FLAG_LOCAL_CLIP_VALID 0x0004

#define FLAGS_ALL             (FLAG_CLIP_VALID | FLAG_CLIP_CHANGED | FLAG_LOCAL_CLIP_VALID)

int cur_color = 0;
char FillColorName[8][20] = {"Pg_BLACK","Pg_BLUE","Pg_RED","Pg_YELLOW","Pg_GREEN","Pg_MAGENTA","Pg_CYAN","Pg_WHITE"};
long FillColorVal[8] = {Pg_BLACK,Pg_BLUE,Pg_RED,Pg_YELLOW,Pg_GREEN,Pg_MAGENTA,Pg_CYAN,Pg_WHITE};

// Macro for creating a palette relative color if you have a COLORREF instead
// of the reg, green, and blue values. The color is color-matches to the nearest
// in the current logical palette. This has no effect on a non-palette device
#define PALETTERGB_COLORREF(c)  (0x02000000 | (c))

// Macro for converting from nscolor to PtColor_t
// Photon RGB values are stored as 00 RR GG BB
// nscolor RGB values are 00 BB GG RR
#define NS_TO_PH_RGB(ns) (ns & 0xff) << 16 | (ns & 0xff00) | ((ns >> 16) & 0xff)
#define PH_TO_NS_RGB(ns) (ns & 0xff) << 16 | (ns & 0xff00) | ((ns >> 16) & 0xff)

#ifdef DEBUG
// By creating "/dev/shmem/grab" this enables a functions that takes the
// offscreen buffer and stuffs it into "grab.bmp" to be viewed by the
// developer for debug purposes...

#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int x=0,y=0;
int X,Y,DEPTH;
int real_depth;
int scale=1;

static void do_bmp(char *ptr,int bpl,int x,int y);
#endif

#include <prlog.h>
PRLogModuleInfo *PhGfxLog = PR_NewLogModule("PhGfxLog");
#include "nsPhGfxLog.h"

NS_IMPL_ISUPPORTS1(nsRenderingContextPh, nsIRenderingContext)


/* Global Variable for Alpha Blending */
void *Mask = nsnull;

/* The default Photon Drawing Context */
PhGC_t *nsRenderingContextPh::mPtGC = nsnull;

#define SELECT(surf) mBufferIsEmpty = PR_FALSE; if (surf->Select()) ApplyClipping(surf->GetGC());
//#define SELECT(surf) if (surf->Select()) ApplyClipping(surf->GetGC());

#define PgFLUSH() PgFlush()

nsRenderingContextPh :: nsRenderingContextPh()
{
  NS_INIT_REFCNT();
  
  mGC = nsnull;
  mTMatrix         = new nsTransform2D();
  mClipRegion          = nsnull ; // new nsRegionPh();
  //mClipRegion->Init();
  mFontMetrics     = nsnull;
  mSurface         = nsnull;
  mMainSurface     = nsnull;
  mDCOwner         = nsnull;
  mContext         = nsnull;
  mP2T             = 1.0f;
  mWidget          = nsnull;
  mPhotonFontName  = nsnull;
  Mask             = nsnull;
  mCurrentLineStyle       = nsLineStyle_kSolid;

  //default objects
  //state management

  mStates          = nsnull;
  mStateCache      = new nsVoidArray();
  mGammaTable      = nsnull;
  
  if( mPtGC == nsnull )
    mPtGC = PgGetGC();

  mInitialized   = PR_FALSE;
  mBufferIsEmpty = PR_TRUE;

  PushState();
}


nsRenderingContextPh :: ~nsRenderingContextPh()
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::~nsRenderingContextPh this=<%p> mGC = %p\n", this, mGC ));

  // Destroy the State Machine
  if (mStateCache)
  {
    PRInt32 cnt = mStateCache->Count();

    while (--cnt >= 0)
    {
      PRBool  clipstate;
      PopState(clipstate);
    }

    delete mStateCache;
    mStateCache = nsnull;
  }

  if (mTMatrix)
    delete mTMatrix;

  if (!mSurface)
  {
    if( mGC )
    {
      PgSetGC( mPtGC );
      PgSetRegion( mPtGC->rid );
      PgDestroyGC( mGC );
      mGC = nsnull;
    }
  }

  /* We always do this?? */
  PgSetGC( mPtGC );
  PgSetRegion( mPtGC->rid );

  if (mPhotonFontName)
    delete [] mPhotonFontName;

  NS_IF_RELEASE(mClipRegion);  /* do we need to do this? */
  NS_IF_RELEASE(mFontMetrics);
  NS_IF_RELEASE(mContext);
}


NS_IMETHODIMP nsRenderingContextPh :: Init(nsIDeviceContext* aContext,
                                           nsIWidget *aWindow)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Init with a widget aContext=<%p> aWindow=<%p>\n", aContext, aWindow));
  NS_PRECONDITION(PR_FALSE == mInitialized, "double init");

  nsresult res;

  mContext = aContext;
  NS_IF_ADDREF(mContext);

  mWidget = (PtWidget_t*) aWindow->GetNativeData( NS_NATIVE_WIDGET );

  if(!mWidget)
  {
    NS_IF_RELEASE(mContext); // new
    NS_ASSERTION(mWidget,"nsRenderingContext::Init (with a widget) mWidget is NULL!");
    return NS_ERROR_FAILURE;
  }

  PhRid_t    rid = PtWidgetRid( mWidget );
  
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Init this=<%p> mWidget=<%p> rid=<%d>\n", this, mWidget, rid ));

  if (rid == 0)
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Init Widget (%p) does not have a Rid!\n", mWidget ));
  }
  else
  {
    mGC = PgCreateGC( 4096 );
    if( !mGC )
    {
      PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Init PgCreateGC() failed!\n" ));
    }

    NS_ASSERTION(mGC, "nsRenderingContextPh::Init PgCreateGC() failed!");
  
    PgSetGC( mGC );
    PgDefaultGC( mGC );
    PgSetRegion( rid );

    mSurface = new nsDrawingSurfacePh();
    if (mSurface)
    {
      res = mSurface->Init(mGC);
      if (res != NS_OK)
      {
        PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Init  mSurface->Init(mGC) failed\n"));
        return NS_ERROR_FAILURE;
      }

      mOffscreenSurface = mSurface;
      NS_ADDREF(mSurface);
    }
    else
    {
      NS_ASSERTION(0, "nsRenderingContextPh::Init Failed to new the mSurface");
      return NS_ERROR_FAILURE;
    }
  }
  
  mInitialized = PR_TRUE;
  return (CommonInit());
}

NS_IMETHODIMP nsRenderingContextPh::CommonInit()
{
  if ( NS_SUCCEEDED(nsComponentManager::CreateInstance(kRegionCID, 0, 
          NS_GET_IID(nsIRegion), (void**)&mClipRegion)) )
  {
    mClipRegion->Init();
    if (mSurface)
	{
      PRUint32 width, height;
	  mSurface->GetDimensions(&width, &height);
      mClipRegion->SetTo(0, 0, width, height);
	}
	else
	{
      mClipRegion->SetTo(0, 0, 0,0);
    }
  }
  else
  {
    // we're going to crash shortly after if we hit this, but we will return NS_ERROR_FAILURE anyways.
    return NS_ERROR_FAILURE;
  }

  NS_ASSERTION(mContext,"nsRenderingContextPh::CommonInit mContext is NULL!");
  NS_ASSERTION(mTMatrix,"nsRenderingContextPh::CommonInit mTMatrix is NULL!");

  if (mContext && mTMatrix)
  {
#if 0
    mContext->GetAppUnitsToDevUnits(app2dev);
    mTMatrix->AddScale(app2dev,app2dev);
    mContext->GetDevUnitsToAppUnits(mP2T);
    mContext->GetGammaTable(mGammaTable);
#else
    mContext->GetDevUnitsToAppUnits(mP2T);
    float app2dev;
    mContext->GetAppUnitsToDevUnits(app2dev);
    mTMatrix->AddScale(app2dev, app2dev);
#endif
  }
  
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: Init(nsIDeviceContext* aContext,
                                           nsDrawingSurface aSurface)
{

  printf ("nsRenderingContextPh::Init  with a surface!!!! %p\n",aSurface);

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Init with a Drawing Surface\n"));

  NS_PRECONDITION(PR_FALSE == mInitialized, "double init");

  mContext = aContext;
  NS_IF_ADDREF(mContext);

  mSurface = (nsDrawingSurfacePh *) aSurface;
  // GTK removed this mOffscreenSurface=mSurface;
  NS_ADDREF(mSurface);

  mInitialized = PR_TRUE;
  return (CommonInit());
}


NS_IMETHODIMP nsRenderingContextPh :: LockDrawingSurface(PRInt32 aX, PRInt32 aY,
                                                          PRUint32 aWidth, PRUint32 aHeight,
                                                          void **aBits, PRInt32 *aStride,
                                                          PRInt32 *aWidthBytes, PRUint32 aFlags)
{
  PushState();

  return mSurface->Lock(aX, aY, aWidth, aHeight,
                        aBits, aStride, aWidthBytes, aFlags);
}

NS_IMETHODIMP nsRenderingContextPh::UnlockDrawingSurface(void)
{
  PRBool  clipstate;
  PopState(clipstate);

  mSurface->Unlock();

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: SelectOffScreenDrawingSurface(nsDrawingSurface aSurface)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SelectOffScreenDrawingSurface this=<%p> sSurface=<%p>\n", this, aSurface));

  if (nsnull==aSurface)
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SelectOffScreenDrawingSurface  selecting offscreen (private)\n"));
    mSurface = mOffscreenSurface;
  }
  else
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SelectOffScreenDrawingSurface  selecting passed-in (%p)\n", aSurface));
    mSurface = (nsDrawingSurfacePh *) aSurface;
  }

//  printf ("kedl2: select pixmap %p\n", ((nsDrawingSurfacePh *)mSurface)->mPixmap);
  mSurface->Select();

// to clear the buffer to black to clean up transient rips during redraw....
#if 1
  PgSetClipping( 0, NULL );
  PgSetMultiClip( 0, NULL );
  PgSetFillColor(FillColorVal[cur_color]);
  //PgDrawIRect( 0, 0, 1024,768, Pg_DRAW_FILL_STROKE ); 
  cur_color++;
  cur_color &= 0x7;
#endif

  mBufferIsEmpty = PR_TRUE;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextPh :: GetDrawingSurface(nsDrawingSurface *aSurface)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetDrawingSurface\n"));
//  printf ("get drawing surface! %p\n",mSurface);
  *aSurface = (void *) mSurface;
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: GetHints(PRUint32& aResult)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetHints\n"));

  PRUint32 result = 0;

  // Most X servers implement 8 bit text rendering alot faster than
  // XChar2b rendering. In addition, we can avoid the PRUnichar to
  // XChar2b conversion. So we set this bit...
  result |= NS_RENDERING_HINT_FAST_8BIT_TEXT;
  
  
  /* this flag indicates that the system prefers 8bit chars over wide chars */
  /* It may or may not be faster under photon... */
  
  aResult = result;

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: Reset()
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Reset  - Not Implemented\n"));
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: GetDeviceContext(nsIDeviceContext *&aContext)
{
//  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetDeviceContext\n"));

  NS_IF_ADDREF( mContext );
  aContext = mContext;
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: PushState(void)
{
  //PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::PushState\n"));

  //  Get a new GS
#ifdef USE_GS_POOL
  nsGraphicsState *state = nsGraphicsStatePool::GetNewGS();
#else
  nsGraphicsState *state = new nsGraphicsState;
#endif

  // Push into this state object, add to vector
  if (!state)
  {
    NS_ASSERTION(0, "nsRenderingContextPh::PushState Failed to create a new Graphics State");
    return NS_ERROR_FAILURE;
  }

  state->mMatrix = mTMatrix;

  if (nsnull == mTMatrix)
    mTMatrix = new nsTransform2D();
  else
    mTMatrix = new nsTransform2D(mTMatrix);

  if (mClipRegion)
  {
    // set the state's clip region to a new copy of the current clip region
    GetClipRegion(&state->mClipRegion);
  }

  NS_IF_ADDREF(mFontMetrics);
  state->mFontMetrics = mFontMetrics;

  state->mColor = mCurrentColor;
  state->mLineStyle = mCurrentLineStyle;

  mStateCache->AppendElement(state);
	
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextPh :: PopState( PRBool &aClipEmpty )
{
  //PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::PopState\n"));

  PRUint32 cnt = mStateCache->Count();
  nsGraphicsState * state;

  if (cnt > 0) {
    state = (nsGraphicsState *)mStateCache->ElementAt(cnt - 1);
    mStateCache->RemoveElementAt(cnt - 1);

    // Assign all local attributes from the state object just popped
    if (mTMatrix)
      delete mTMatrix;
    mTMatrix = state->mMatrix;

    // get rid of the current clip region
    NS_IF_RELEASE(mClipRegion);
    mClipRegion = nsnull;

    // restore everything
    mClipRegion = (nsRegionPh *) state->mClipRegion;
#if 0
    mFontMetrics = state->mFontMetrics;
#else
    if (mFontMetrics != state->mFontMetrics)
	{
      SetFont(state->mFontMetrics);
    }
#endif

    if (mSurface && mClipRegion)
    {
       //ApplyClipping(mGC);
    }

    ApplyClipping(mGC);

    if (state->mColor != mCurrentColor)
      SetColor(state->mColor);

    if (state->mLineStyle != mCurrentLineStyle)
      SetLineStyle(state->mLineStyle);

    // Delete this graphics state object
#ifdef USE_GS_POOL
    nsGraphicsStatePool::ReleaseGS(state);
#else
    delete state;
#endif
  }

  if (mClipRegion)
    aClipEmpty = mClipRegion->IsEmpty();
  else
    aClipEmpty = PR_TRUE;

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: IsVisibleRect(const nsRect& aRect, PRBool &aVisible)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::IsVisibleRect - Not Implemented\n"));
  aVisible = PR_TRUE;
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: SetClipRect(const nsRect& aRect, nsClipCombine aCombine, PRBool &aClipEmpty)
{
  nsresult   res = NS_ERROR_FAILURE;
  nsRect     trect = aRect;
  PhRect_t  *rgn;

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SetClipRect  (%ld,%ld,%ld,%ld)\n", aRect.x, aRect.y, aRect.width, aRect.height ));

  if ((mTMatrix) && (mClipRegion))
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  prev clip empty = %i\n", mClipRegion->IsEmpty()));

    mTMatrix->TransformCoord(&trect.x, &trect.y,&trect.width, &trect.height);

    switch(aCombine)
    {
      case nsClipCombine_kIntersect:
   	PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  combine type = intersect\n"));
        mClipRegion->Intersect(trect.x,trect.y,trect.width,trect.height);
        break;
      case nsClipCombine_kUnion:
   	PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  combine type = union\n"));
        mClipRegion->Union(trect.x,trect.y,trect.width,trect.height);
        break;
      case nsClipCombine_kSubtract:
   	PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  combine type = subtract\n"));
        mClipRegion->Subtract(trect.x,trect.y,trect.width,trect.height);
        break;
      case nsClipCombine_kReplace:
   	PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  combine type = replace\n"));
        mClipRegion->SetTo(trect.x,trect.y,trect.width,trect.height);
        break;
      default:
   	PR_LOG(PhGfxLog, PR_LOG_ERROR, ("nsRenderingContextPh::SetClipRect  Unknown Combine type\n"));
        break;
    }

    aClipEmpty = mClipRegion->IsEmpty();
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  new clip empty = %i\n", aClipEmpty ));

    ApplyClipping(mGC);
    res = NS_OK;
  }
  else
  {
    printf ("no region....\n");
    PR_LOG(PhGfxLog, PR_LOG_ERROR, ("nsRenderingContextPh::SetClipRect  Invalid pointers!\n"));
  }
  																			
  return res;
}

NS_IMETHODIMP nsRenderingContextPh :: GetClipRect(nsRect &aRect, PRBool &aClipValid)
{
  PRInt32 x, y, w, h;

  if (!mClipRegion->IsEmpty())
  {
    mClipRegion->GetBoundingBox(&x,&y,&w,&h);
    aRect.SetRect(x,y,w,h);
    aClipValid = PR_TRUE;
  }
  else
  {
    aRect.SetRect(0,0,0,0);
    aClipValid = PR_FALSE;
  }

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetClipRect aClipValid=<%d> rect=(%d,%d,%d,%d)\n", aClipValid,aRect.x,aRect.y,aRect.width,aRect.height));

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: SetClipRegion(const nsIRegion& aRegion, nsClipCombine aCombine, PRBool &aClipEmpty)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SetClipRegion\n"));

  switch(aCombine)
  {
  case nsClipCombine_kIntersect:
    mClipRegion->Intersect(aRegion);
    break;
  case nsClipCombine_kUnion:
    mClipRegion->Union(aRegion);
    break;
  case nsClipCombine_kSubtract:
    mClipRegion->Subtract(aRegion);
    break;
  case nsClipCombine_kReplace:
    mClipRegion->SetTo(aRegion);
    break;
  }

  aClipEmpty = mClipRegion->IsEmpty();
  ApplyClipping(mGC);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextPh :: CopyClipRegion(nsIRegion &aRegion)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyClipRegion\n"));
  aRegion.SetTo(*NS_STATIC_CAST(nsIRegion*, mClipRegion));

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsRenderingContextPh :: GetClipRegion(nsIRegion **aRegion)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetClipRegion\n"));
  nsresult rv = NS_ERROR_FAILURE;

  if (!aRegion)
    return NS_ERROR_NULL_POINTER;

  if (*aRegion) // copy it, they should be using CopyClipRegion
  {
    // printf("you should be calling CopyClipRegion()\n");
    (*aRegion)->SetTo(*mClipRegion);
    rv = NS_OK;
  }
  else
  {
    if ( NS_SUCCEEDED(nsComponentManager::CreateInstance(kRegionCID, 0, NS_GET_IID(nsIRegion), 
                                                         (void**)aRegion )) )
    {
      if (mClipRegion)
      {
        (*aRegion)->Init();
        (*aRegion)->SetTo(*mClipRegion);
        NS_ADDREF(*aRegion);
        rv = NS_OK;
      }
      else
      {
        printf("null clip region, can't make a valid copy\n");
        NS_RELEASE(*aRegion);
        rv = NS_ERROR_FAILURE;
      }
    } 
  }

  return rv;
}


NS_IMETHODIMP nsRenderingContextPh :: SetColor(nscolor aColor)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SetColor (%i,%i,%i)\n", NS_GET_R(aColor), NS_GET_G(aColor), NS_GET_B(aColor) ));

  if (nsnull == mContext)  
    return NS_ERROR_FAILURE;
	
  mCurrentColor = aColor;

  PgSetStrokeColor( NS_TO_PH_RGB( aColor ));
  PgSetFillColor( NS_TO_PH_RGB( aColor ));
  PgSetTextColor( NS_TO_PH_RGB( aColor ));

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: GetColor(nscolor &aColor) const
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetColor\n"));
  aColor = mCurrentColor;
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: SetLineStyle(nsLineStyle aLineStyle)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SetLineStyle\n"));
  mCurrentLineStyle = aLineStyle;
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: GetLineStyle(nsLineStyle &aLineStyle)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetLineStyle  - Not Implemented\n"));
  aLineStyle = mCurrentLineStyle;
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: SetFont(const nsFont& aFont)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SetFont with nsFont\n"));

#if 0
  if (mFontMetrics)
    NS_IF_RELEASE(mFontMetrics);

  if (mContext)
  {
    mContext->GetMetricsFor(aFont, mFontMetrics);
    return SetFont(mFontMetrics);
  }
  else
    return NS_ERROR_FAILURE;
#else
  nsIFontMetrics* newMetrics;
  nsresult rv = mContext->GetMetricsFor(aFont, newMetrics);
  if (NS_SUCCEEDED(rv)) {
    rv = SetFont(newMetrics);
    NS_RELEASE(newMetrics);
  }
  return rv;
#endif
}


NS_IMETHODIMP nsRenderingContextPh :: SetFont(nsIFontMetrics *aFontMetrics)
{
//  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SetFont with nsIFontMetrics mFontMetrics=<%p> aFontMetrics=<%p>\n", mFontMetrics, aFontMetrics));
	  
  nsFontHandle  fontHandle;			/* really a nsString */
  nsString      *pFontHandle;

  NS_IF_RELEASE(mFontMetrics);
  mFontMetrics = aFontMetrics;
  NS_IF_ADDREF(mFontMetrics);

  if (mFontMetrics == nsnull)
    return NS_OK;

  mFontMetrics->GetFontHandle(fontHandle);
  pFontHandle = (nsString *) fontHandle;
    
  if (pFontHandle)
  {  
    if( mPhotonFontName )
      delete [] mPhotonFontName;

    mPhotonFontName = pFontHandle->ToNewCString();

	/* Cache the Font metrics locally, costs ~1400 bytes per font */
    PfLoadMetrics( mPhotonFontName );

    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::SetFont with nsIFontMetrics Photon Font Name is <%s>\n", mPhotonFontName));

    PgSetFont( mPhotonFontName );
  }
  else
  {
    PR_LOG(PhGfxLog, PR_LOG_ERROR, ("nsRenderingContextPh::SetFont with nsIFontMetrics, INVALID Font Handle\n"));
  }
	
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: GetFontMetrics(nsIFontMetrics *&aFontMetrics)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetFontMetrics mFontMetrics=<%p>\n", mFontMetrics));

  NS_IF_ADDREF(mFontMetrics);
  aFontMetrics = mFontMetrics;
  return NS_OK;
}


// add the passed in translation to the current translation
NS_IMETHODIMP nsRenderingContextPh :: Translate(nscoord aX, nscoord aY)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Translate (%i,%i)\n", aX, aY));
  mTMatrix->AddTranslation((float)aX,(float)aY);
  return NS_OK;
}


// add the passed in scale to the current scale
NS_IMETHODIMP nsRenderingContextPh :: Scale(float aSx, float aSy)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::Scale (%f,%f)\n", aSx, aSy ));
  mTMatrix->AddScale(aSx, aSy);
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: GetCurrentTransform(nsTransform2D *&aTransform)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetCurrentTransform\n"));
  aTransform = mTMatrix;
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: CreateDrawingSurface(nsRect *aBounds, PRUint32 aSurfFlags, nsDrawingSurface &aSurface)
{
// REVISIT; what are the flags???

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CreateDrawingSurface\n"));

  if (nsnull==mSurface) {
    aSurface = nsnull;
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  mSurface is NULL - failure!\n"));
    return NS_ERROR_FAILURE;
  }

 nsDrawingSurfacePh *surf = new nsDrawingSurfacePh();

//printf ("create2: %p %d\n",surf,aSurfFlags);

  if (surf)
  {
    NS_ADDREF(surf);
    surf->Init(mSurface->GetGC(), aBounds->width, aBounds->height, aSurfFlags);
  }
  else
  {
    NS_ASSERTION(surf, "nsRenderingContextPh::CreateDrawingSurface new nsDrawingSurfacePh is NULL");
    return NS_ERROR_FAILURE;
  }
   
  aSurface = (nsDrawingSurface) surf;

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  new surface = %p\n", aSurface));

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DestroyDrawingSurface(nsDrawingSurface aDS)
{
  PhImage_t *image;
  void *gc;

   PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DestroyDrawingSurface\n"));

   nsDrawingSurfacePh *surf = (nsDrawingSurfacePh *) aDS;
   NS_IF_RELEASE(surf);

   return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawLine(nscoord aX0, nscoord aY0, nscoord aX1, nscoord aY1)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawLine (%ld,%ld,%ld,%ld)\n", aX0, aY0, aX1, aY1 ));
  nscoord x0,y0,x1,y1;

#if 0

  if( nsLineStyle_kNone == mCurrentLineStyle )
    return NS_OK;

  x0 = aX0;
  y0 = aY0;
  x1 = aX1;
  y1 = aY1;

  mTMatrix->TransformCoord(&x0,&y0);
  mTMatrix->TransformCoord(&x1,&y1);

#else

  mTMatrix->TransformCoord(&aX0,&aY0);
  mTMatrix->TransformCoord(&aX1,&aY1);

  if (aY0 != aY1) {
    aY1--;
  }
  if (aX0 != aX1) {
    aX1--;
  }

  x0 = aX0;
  y0 = aY0;
  x1 = aX1;
  y1 = aY1;
#endif

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawLine (%ld,%ld,%ld,%ld)\n", x0, y0, x1, y1 ));

  SELECT(mSurface);
  SetPhLineStyle();
  PgDrawILine( x0, y0, x1, y1 );

  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawPolyline(const nsPoint aPoints[], PRInt32 aNumPoints)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::DrawPolyLine\n"));

  if( nsLineStyle_kNone == mCurrentLineStyle )
    return NS_OK;

  PhPoint_t *pts;

  if(( pts = new PhPoint_t [aNumPoints] ) != NULL )
  {
    PhPoint_t pos = {0,0};
    PRInt32 i;

    for(i=0;i<aNumPoints;i++)
    {
    int x,y;
      x = aPoints[i].x;
      y = aPoints[i].y;
      mTMatrix->TransformCoord(&x,&y);
      pts[i].x = x;
      pts[i].y = y;
    }

    SELECT(mSurface);
    SetPhLineStyle();
    PgDrawPolygon( pts, aNumPoints, &pos, Pg_DRAW_STROKE );

    delete [] pts;
  }
  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawRect(const nsRect& aRect)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::DrawRect 1 \n"));

  DrawRect( aRect.x, aRect.y, aRect.width, aRect.height );

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::DrawRect 2 \n"));

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;
  mTMatrix->TransformCoord(&x,&y,&w,&h);

  SELECT(mSurface);
  if (w && h)
    PgDrawIRect( x, y, x + w - 1, y + h - 1, Pg_DRAW_STROKE );

  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: FillRect(const nsRect& aRect)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::FillRect 1 (%i,%i,%i,%i)\n", aRect.x, aRect.y, aRect.width, aRect.height ));

  FillRect( aRect.x, aRect.y, aRect.width, aRect.height );

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: FillRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::FillRect 2 (%i,%i,%i,%i)\n", aX, aY, aWidth, aHeight ));
  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);
  SELECT(mSurface);
  PgDrawIRect( x, y, x + w - 1, y + h - 1, Pg_DRAW_FILL_STROKE );

  PgFLUSH();	//kedl
  return NS_OK;
}

NS_IMETHODIMP 
nsRenderingContextPh :: InvertRect(const nsRect& aRect)
{
  InvertRect( aRect.x, aRect.y, aRect.width, aRect.height );

  return NS_OK;
}

// kedl,july 21, 1999
// looks like we crashe on test12 when u try to select; but so does linux
// and windows rips on test12.... yippeeeee; otherwise we look great!
NS_IMETHODIMP 
nsRenderingContextPh :: InvertRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{

 if (nsnull == mTMatrix || nsnull == mSurface) {
    return NS_ERROR_FAILURE;
  }
  
  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  /* Kedl thinks this fixes the blinking cursor crash */
  if (!mSurface)
  	return NS_OK;		// kedl, error instead?

  mTMatrix->TransformCoord(&x,&y,&w,&h);
  SELECT(mSurface);
  //printf ("invert rect: %d %d %d %d\n",x,y,w,h);

  PgSetFillColor(Pg_INVERT_COLOR);
  PgSetDrawMode(Pg_DRAWMODE_XOR);
  PgDrawIRect( x, y, x + w - 1, y + h - 1, Pg_DRAW_FILL );
  PgSetDrawMode(Pg_DRAWMODE_OPAQUE);

  PgFLUSH();	//kedl
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextPh :: DrawPolygon(const nsPoint aPoints[], PRInt32 aNumPoints)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::DrawPolygon\n"));

  PhPoint_t *pts;

  if(( pts = new PhPoint_t [aNumPoints] ) != NULL )
  {
    PhPoint_t pos = {0,0};
    PRInt32 i;

    for(i=0;i<aNumPoints;i++)
    {
    int x,y;
      x = aPoints[i].x;
      y = aPoints[i].y;
      mTMatrix->TransformCoord(&x,&y);
      pts[i].x = x;
      pts[i].y = y;
    }

    SELECT(mSurface);
    PgDrawPolygon( pts, aNumPoints, &pos, Pg_DRAW_STROKE | Pg_CLOSED );

    delete [] pts;
  }
  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: FillPolygon(const nsPoint aPoints[], PRInt32 aNumPoints)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::FillPolygon aNumPoints=%d\n", aNumPoints));

#if 0
 return NS_OK;
#else  
  PhPoint_t *pts;
  int err;
  
  if(( pts = new PhPoint_t [aNumPoints] ) != NULL )
  {
    PhPoint_t pos = {0,0};
    PRInt32 i,c;
    int x,y;

	  /* Put the first point into pts */
      x = aPoints[0].x;
      y = aPoints[0].y;
      mTMatrix->TransformCoord(&x,&y);
      pts[0].x = x;
      pts[0].y = y;	  

    for(i=1,c=0;i<aNumPoints;i++)
    {
      x = aPoints[i].x;
      y = aPoints[i].y;
      mTMatrix->TransformCoord(&x,&y);

      PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::FillPolygon %d (%d,%d) -> (%d,%d) \n", i, aPoints[i].x, aPoints[i].y, x, y));

      if ((pts[c].x != x) || (pts[c].y != y))
	  {
		c++;
        pts[c].x = x;
        pts[c].y = y;
      }
    }

    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::FillPolygon calling SELECT with %d points\n", (c+1) ));

    SELECT(mSurface);
//    err=PgDrawPolygon( pts, (c+1), &pos, Pg_DRAW_FILL_STROKE | Pg_CLOSED );
    err=PgDrawPolygon( pts, (c+1), &pos, Pg_DRAW_FILL_STROKE );

    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::FillPolygon after PgDrawPolgon err=<%d>\n", err));

    delete [] pts;
  }

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::FillPolygon before PgFlush \n"));

  PgFLUSH();	//kedl

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("untested nsRenderingContextPh::FillPolygon after PgFlush \n"));

  return NS_OK;
#endif
}


NS_IMETHODIMP nsRenderingContextPh :: DrawEllipse(const nsRect& aRect)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawEllipse.\n"));

  DrawEllipse( aRect.x, aRect.y, aRect.width, aRect.height );

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawEllipse.\n"));
  nscoord x,y,w,h;
  PhPoint_t center;
  PhPoint_t radii;
  unsigned int flags;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  center.x = x;
  center.y = y;
  radii.x = x+w-1;
  radii.y = y+h-1;
  flags = Pg_EXTENT_BASED | Pg_DRAW_STROKE;
  SELECT(mSurface);
  PgDrawEllipse( &center, &radii, flags );

  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: FillEllipse(const nsRect& aRect)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::FillEllipse.\n"));

  FillEllipse( aRect.x, aRect.y, aRect.width, aRect.height );

  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: FillEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::FillEllipse.\n"));
  nscoord x,y,w,h;
  PhPoint_t center;
  PhPoint_t radii;
  unsigned int flags;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  center.x = x;
  center.y = y;
  radii.x = x+w-1;
  radii.y = y+h-1;
  flags = Pg_EXTENT_BASED | Pg_DRAW_FILL_STROKE;
  SELECT(mSurface);
  PgDrawEllipse( &center, &radii, flags );

  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawArc(const nsRect& aRect,
                                 float aStartAngle, float aEndAngle)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawArc - Not implemented.\n"));

  return DrawArc(aRect.x,aRect.y,aRect.width,aRect.height,aStartAngle,aEndAngle);
}


NS_IMETHODIMP nsRenderingContextPh :: DrawArc(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight,
                                 float aStartAngle, float aEndAngle)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawArc\n"));
  nscoord x,y,w,h;
  PhPoint_t center;
  PhPoint_t radii;
  unsigned int flags;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  center.x = x;
  center.y = y;
  radii.x = x+w-1;
  radii.y = y+h-1;
  flags = Pg_EXTENT_BASED | Pg_DRAW_STROKE;
  SELECT(mSurface);
  PgDrawArc( &center, &radii, aStartAngle, aEndAngle, flags );

  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: FillArc(const nsRect& aRect,
                                 float aStartAngle, float aEndAngle)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::FillArc\n"));
  return FillArc(aRect.x,aRect.y,aRect.width,aRect.height,aStartAngle,aEndAngle);
}


NS_IMETHODIMP nsRenderingContextPh :: FillArc(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight,
                                 float aStartAngle, float aEndAngle)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::FillArc - Not implemented.\n"));
  nscoord x,y,w,h;
  PhPoint_t center;
  PhPoint_t radii;
  unsigned int flags;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  center.x = x;
  center.y = y;
  radii.x = x+w-1;
  radii.y = y+h-1;
  flags = Pg_EXTENT_BASED | Pg_DRAW_FILL_STROKE;
  SELECT(mSurface);
  PgDrawArc( &center, &radii, aStartAngle, aEndAngle, flags );

  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: GetWidth(char ch, nscoord& aWidth)
{
  char buf[2];
  nsresult ret_code;

  /* turn it into a string */
  buf[0] = ch;
  buf[1] = nsnull;

  ret_code = GetWidth(buf, 1, aWidth);  
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth1 for <%c> aWidth=<%d> ret_code=<%d>\n", ch, aWidth, ret_code));

  return ret_code;
}


NS_IMETHODIMP nsRenderingContextPh :: GetWidth(PRUnichar ch, nscoord &aWidth, PRInt32 *aFontID)
{
  PRUnichar buf[2];
  nsresult ret_code;

  /* turn it into a string */
  buf[0] = ch;
  buf[1] = nsnull;

  ret_code = GetWidth(buf, 1, aWidth, aFontID);  
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth2 for <%c> aWidth=<%d> aFontId=<%p> ret_code=<%d>\n", (char) ch, aWidth, aFontID, ret_code));
  return ret_code;
}


NS_IMETHODIMP nsRenderingContextPh :: GetWidth(const char* aString, nscoord& aWidth)
{
  nsresult ret_code;

  ret_code = GetWidth(aString, strlen(aString), aWidth);  
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth3 for <%s> aWidth=<%d> ret_code=<%d>\n", aString, aWidth, ret_code));

  return ret_code;
}


NS_IMETHODIMP nsRenderingContextPh :: GetWidth(const char* aString,
                                                PRUint32 aLength,
                                                nscoord& aWidth)
{
  nsresult ret_code = NS_ERROR_FAILURE;
  
  aWidth = 0;	// Initialize to zero in case we fail.

  if (nsnull != mFontMetrics)
  {
    PhRect_t      extent;
	
    if (PfExtentText(&extent, NULL, mPhotonFontName, aString, aLength))
    {
      aWidth = (int) ((extent.lr.x - extent.ul.x + 1) * mP2T);

      ret_code = NS_OK;
    }
  }
  else
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth4 FAILED = a NULL mFontMetrics detected\n"));
    ret_code = NS_ERROR_FAILURE;
  }  

  return ret_code;
}


NS_IMETHODIMP nsRenderingContextPh :: GetWidth(const nsString& aString, nscoord& aWidth, PRInt32 *aFontID)
{
#if 0
  /* DEBUG ONLY */
  char *str = aString.ToNewCString();
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth5 aString=<%s>\n", str));
  delete [] str;
#endif

  nsresult ret_code;

  ret_code = GetWidth(aString.GetUnicode(), aString.Length(), aWidth, aFontID);  

  /* What the heck? I copied this from Windows */
  if (nsnull != aFontID)
    *aFontID = 0;

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth5  aWidth=<%d> ret_code=<%d>\n", aWidth, ret_code));
  return ret_code;
}


NS_IMETHODIMP nsRenderingContextPh :: GetWidth(const PRUnichar *aString,
                                                PRUint32 aLength,
                                                nscoord &aWidth,
                                                PRInt32 *aFontID)
{
  nsresult ret_code = NS_ERROR_FAILURE;
  nscoord photonWidth;
  
  aWidth = 0;	// Initialize to zero in case we fail.

  if (nsnull != mFontMetrics)
  {
    PhRect_t      extent;
//    nsFontHandle  fontHandle;			/* really a (nsString  *) */
//    nsString      *pFontHandle = nsnull;
//    char          *PhotonFontName =  nsnull;

//    mFontMetrics->GetFontHandle(fontHandle);
//    pFontHandle = (nsString *) fontHandle;
//    PhotonFontName =  pFontHandle->ToNewCString();
	
    if (PfExtentWideText(&extent, NULL, mPhotonFontName, (wchar_t *) aString, (aLength*2)))
    {
//	  photonWidth = (extent.lr.x - extent.ul.x + 1);
// 	  aWidth = (int) ((float) photonWidth * mP2T);
      aWidth = (int) ((extent.lr.x - extent.ul.x + 1) * mP2T);
     
//      PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth4 PhotonWidth=<%d> aWidth=<%d> PhotonFontName=<%s>\n",photonWidth, aWidth, PhotonFontName));

      ret_code = NS_OK;
//	  delete [] PhotonFontName;
    }
  }
  else
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth6 FAILED = a NULL mFontMetrics detected\n"));
    ret_code = NS_ERROR_FAILURE;
  }  

  if (nsnull != aFontID)
  {
    *aFontID = 0;
  }
  	

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::GetWidth6  aLength=<%d> aWidth=<%d> ret_code=<%d>\n", aLength, aWidth, ret_code));

  return ret_code;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawString(const char *aString, PRUint32 aLength,
                                                  nscoord aX, nscoord aY,
                                                  const nscoord* aSpacing)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawString1 first aString=<%s> of %d at (%d,%d) aSpacing=<%p>.\n",aString, aLength, aX, aY, aSpacing));

  int err;
  
  nscoord x = aX;
  nscoord y = aY;

  if (nsnull != aSpacing)
  {
/* REVISIT this code will break with an ACTUAL multi-byte multi-byte string */
    // Render the string, one character at a time...
    const char* end = aString + aLength;
	while (aString < end)
	{
	  char ch = *aString++;
      nscoord xx = x;
	  nscoord yy = y;
	  mTMatrix->TransformCoord(&xx, &yy);
      PhPoint_t pos = { xx, yy };
      SELECT(mSurface);
      PgDrawText( &ch, 1, &pos, (Pg_TEXT_LEFT | Pg_TEXT_TOP));
      x += *aSpacing++;
    }
  }
  else
  {
    mTMatrix->TransformCoord(&x,&y);
    PhPoint_t pos = { x, y };
 	
    SELECT(mSurface);

  /* HACK to see if we have a clipping problem */
  //PgSetClipping(0,NULL);

#if 0
  printf("nsRenderingContextPh::DrawString1 buffer=");
  for(int i=0; i<(aLength*2); i++)
  {
    printf("%X,", *(aString+i));  
  }
  printf("\n");
#endif

  /* Kirk: 10/22/99  HACK HACK should be fix'd  Bugzilla 16886 */
  /* Don't print 1 character strings that are &NBSP; */
  if ( (aLength == 1) && (*aString == 0xFFFFFFA0))
    return NS_OK;

    err=PgDrawTextChars( aString, aLength, &pos, (Pg_TEXT_LEFT | Pg_TEXT_TOP));
    if ( err == -1)
	{
	  printf("nsRenderingContextPh::DrawString1 returned error code\n");
	}
  }

  PgFLUSH();	//kedl
  return NS_OK;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawString(const PRUnichar *aString, PRUint32 aLength,
                                                  nscoord aX, nscoord aY,
                                                  PRInt32 aFontID,
                                                  const nscoord* aSpacing)
{
  const int BUFFER_SIZE = (aLength * 3);
  char buffer[BUFFER_SIZE];
  int len;
  
  len = wcstombs(buffer, (wchar_t *) aString, BUFFER_SIZE);
  return DrawString( (char *) buffer, aLength, aX, aY, aSpacing);
}


NS_IMETHODIMP nsRenderingContextPh :: DrawString(const nsString& aString,
                                                  nscoord aX, nscoord aY,
                                                  PRInt32 aFontID,
                                                  const nscoord* aSpacing)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawString3 at (%d,%d) aSpacing=<%p>.\n", aX, aY, aSpacing));

  return DrawString(aString.GetUnicode(), aString.Length(),
                      aX, aY, aFontID, aSpacing);
}


NS_IMETHODIMP nsRenderingContextPh :: DrawImage(nsIImage *aImage, nscoord aX, nscoord aY)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawImage1\n"));

  nscoord width, height;

  // we have to do this here because we are doing a transform below
  width = NSToCoordRound(mP2T * aImage->GetWidth());
  height = NSToCoordRound(mP2T * aImage->GetHeight());

  return DrawImage(aImage, aX, aY, width, height);
}


NS_IMETHODIMP nsRenderingContextPh :: DrawImage(nsIImage *aImage, nscoord aX, nscoord aY,
                                        nscoord aWidth, nscoord aHeight) 
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawImage2\n"));

  nsresult res;
  nscoord x,y,w,h;

  if (mClipRegion->IsEmpty())
  {
    // this is bad!
     PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawImage2 drawing image with empty clip region\n"));
     printf("nsRenderingContextPh::DrawImage2 drawing image with empty clip region\n");
     return NS_ERROR_FAILURE;
  }
  
  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  SELECT(mSurface);
  res = aImage->Draw( *this, mSurface, x, y, w, h );
  Mask = aImage->GetAlphaBits();

#ifdef DEBUG
{
/*
  PhImage_t *image;
  PRUint32 w, h;
  
  image = ((nsDrawingSurfacePh *)mSurface)->mPixmap;
  ((nsDrawingSurfacePh *)mSurface)->GetDimensions(&w,&h);
  
  unsigned char *ptr;
  ptr = image->image;  

    do_bmp(ptr,image->bpl/3,w,h);
*/
}
#endif

  PgFLUSH();	//kedl
  return res;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawImage(nsIImage *aImage, const nsRect& aSRect, const nsRect& aDRect)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawImage3\n"));

  nsresult res;
  nsRect	sr,dr;

  sr = aSRect;
  mTMatrix->TransformCoord(&sr.x,&sr.y,&sr.width,&sr.height);

  dr = aDRect;
  mTMatrix->TransformCoord(&dr.x,&dr.y,&dr.width,&dr.height);

  SELECT(mSurface);
  res = aImage->Draw(*this,mSurface,sr.x,sr.y,sr.width,sr.height, dr.x,dr.y,dr.width,dr.height);
  Mask = aImage->GetAlphaBits();

  return res;
}


NS_IMETHODIMP nsRenderingContextPh :: DrawImage(nsIImage *aImage, const nsRect& aRect)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::DrawImage4\n"));

  return DrawImage(aImage,
                   aRect.x,
                   aRect.y,
                   aRect.width,
                   aRect.height);
}

static int count=0;

NS_IMETHODIMP nsRenderingContextPh :: CopyOffScreenBits(nsDrawingSurface aSrcSurf,
                                                         PRInt32 aSrcX, PRInt32 aSrcY,
                                                         const nsRect &aDestBounds,
                                                         PRUint32 aCopyFlags)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits this=<%p> aSrcSurf=<%p> aSrcPt=(%d,%d) aCopyFlags=<%d> DestRect=<%d,%d,%d,%d>\n",
     this, aSrcSurf, aSrcX, aSrcY, aCopyFlags, aDestBounds.x, aDestBounds.y, aDestBounds.width, aDestBounds.height));

//printf("nsRenderingContextPh::CopyOffScreenBits 0\n");

  PhArea_t              area;
  PRInt32               srcX = aSrcX;
  PRInt32               srcY = aSrcY;
  nsRect                drect = aDestBounds;
  nsDrawingSurfacePh    *destsurf;

  if ( (aSrcSurf==NULL) || (mTMatrix==NULL) || (mSurface==NULL))
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits  Started with NULL pointer"));
    NS_ASSERTION(0, "nsRenderingContextPh::CopyOffScreenBits Started with NULL pointer");
	printf("nsRenderingContextPh::CopyOffScreenBits Started with NULL pointer\n");

    return NS_ERROR_FAILURE;  
  }
  
  PhGC_t *saveGC = PgGetGC();

  if (aCopyFlags & NS_COPYBITS_TO_BACK_BUFFER)
  {
    NS_ASSERTION(!(nsnull == mSurface), "no back buffer");
    destsurf = mSurface;
  }
  else
    destsurf = mOffscreenSurface;

  /* This is really needed.... */
  if ( (mBufferIsEmpty) && (aCopyFlags != 12))
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits Buffer empty, skipping.\n"));
    printf("nsRenderingContextPh::CopyOffScreenBits Buffer empty, skipping.\n");

    SELECT( destsurf );
    PgSetGC(saveGC);

    return NS_OK;
  }

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("  flags=%X\n", aCopyFlags ));

#if 1
  if (aCopyFlags & NS_COPYBITS_USE_SOURCE_CLIP_REGION)
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits NS_COPYBITS_USE_SOURCE_CLIP_REGION flag enabled\n"));

  if (aCopyFlags & NS_COPYBITS_XFORM_SOURCE_VALUES)
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits NS_COPYBITS_XFORM_SOURCE_VALUES flag enabled\n"));

  if (aCopyFlags & NS_COPYBITS_XFORM_DEST_VALUES)
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits NS_COPYBITS_XFORM_DEST_VALUES flag enabled\n"));

  if (aCopyFlags & NS_COPYBITS_TO_BACK_BUFFER)
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits NS_COPYBITS_TO_BACK_BUFFER flag enabled\n"));
#endif

  if (aCopyFlags & NS_COPYBITS_XFORM_SOURCE_VALUES)
    mTMatrix->TransformCoord(&srcX, &srcY);

  if (aCopyFlags & NS_COPYBITS_XFORM_DEST_VALUES)
    mTMatrix->TransformCoord(&drect.x, &drect.y, &drect.width, &drect.height);

  area.pos.x=drect.x;
  area.pos.y=drect.y;
  area.size.w=drect.width;
  area.size.h=drect.height;

  printf ("nsRenderingContextPh::CopyOffScreenBits 1 CopyFlags=<%d>, SrcSurf=<%p> DestSurf=<%p> Src=(%d,%d) Area=(%d,%d,%d,%d)\n",
    aCopyFlags,aSrcSurf,destsurf,srcX,srcY,area.pos.x,area.pos.y,area.size.w,area.size.h);

  nsRect rect;
  PRBool valid;
  GetClipRect(rect,valid);

  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits clip valid=<%d> rect=<%d,%d,%d,%d>\n",valid, rect.x,rect.y,rect.width,rect.height));

#if 0
/* this is shit */
  if (valid)
  {
    printf ("nsRenderingContextPh::CopyOffScreenBits clip rect=<%d,%d,%d,%d>\n",
	  rect.x,rect.y,rect.width,rect.height);
    area.size.w = rect.width; 
    area.size.h = rect.height; 
  }
#endif
  
  ((nsDrawingSurfacePh *)aSrcSurf)->Stop();
  PhImage_t *image;
  PhImage_t *image2;
  image = ((nsDrawingSurfacePh *)aSrcSurf)->mPixmap;
  image2= ((nsDrawingSurfacePh *)destsurf)->mPixmap;
  SELECT(destsurf);

if (aSrcSurf==destsurf)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits SrcSurf == destsurf image=<%p>\n", image));
  
  if (image==0)
  {
	NS_ASSERTION(image, "nsRenderingContextPh::CopyOffScreenBits: Unsupported onscreen to onscreen copy!!\n");
	return NS_ERROR_FAILURE;
  }
  else
  {
    PhPoint_t pos = { area.pos.x,area.pos.y };
    PhDim_t size = { area.size.w,area.size.h };

    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::CopyOffScreenBits pos=(%d,%d) area=(%d,%d) image->image=<%p> srcY=<%d> srcX=<%d> offset=<%d> image->mask_bm=<%p>\n", pos.x,pos.y, size.w, size.h, image->image, srcY, srcX, (image->bpl * srcY + srcX*3), image->mask_bm));

    unsigned char *ptr;
    ptr = image->image;
    ptr += image->bpl * srcY + srcX*3 ;
//    int err = PgDrawImagemx( ptr, image->type , &pos, &size, image->bpl, 0); 
    /* removed mx just in case! */
    int err = PgDrawImage( ptr, image->type , &pos, &size, image->bpl, 0); 
    if (err == -1)
	{
	  printf ("nsRenderingContextPh::CopyOffScreenBits Error calling PgDrawImage\n");
	}
#ifdef DEBUG
    do_bmp(ptr,image->bpl/3,size.w,size.h);
#endif
  }
}
  else
  {
  PhPoint_t pos = { 0,0 };
  if (aCopyFlags == 12) 	// oh god, super hack.. ==12
  {
   pos.x=area.pos.x;
   pos.y=area.pos.y;
  }
  PhDim_t size = { area.size.w,area.size.h };
  unsigned char *ptr;
  ptr = image->image;
//  PgDrawImagemx( ptr, image->type , &pos, &size, image->bpl, 0); 
  PgDrawImage( ptr, image->type , &pos, &size, image->bpl, 0); 
#ifdef DEBUG
  do_bmp(ptr,image->bpl/3,size.w,size.h);

/*
  PRUint32 w, h;
  ((nsDrawingSurfacePh *)destsurf)->GetDimensions(&w,&h);
  unsigned char *ptr2;
  if (image2)
  {
    ptr2 = image2->image;
	if (ptr2)
      do_bmp(ptr2,image2->bpl/3,w,h);
    else
	  printf("No image to do_bmp\n");
  }
*/
#endif
  }

  PgSetGC(saveGC);
  PgFLUSH();	//kedl
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextPh::RetrieveCurrentNativeGraphicData(PRUint32 * ngd)
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::RetrieveCurrentNativeGraphicData - Not implemented.\n"));
  if (ngd != nsnull)
    *ngd = nsnull;
	
  return NS_OK;
}

void nsRenderingContextPh :: PushClipState(void)
{
printf ("unimp pushclipstate\n");
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::PushClipState - Not implemented.\n"));
}


void nsRenderingContextPh::ApplyClipping( PhGC_t *gc )
{
  PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::ApplyClipping gc=<%p> mClipRegion=<%p>\n", gc, mClipRegion));

#if 0
  if (!gc)
  {
	NS_ASSERTION(gc, "nsRenderingContextPh::ApplyClipping gc is NULL");
	return;
  }
#endif

  if (mClipRegion)
  {
    int         err;
    PhTile_t    *tiles = nsnull;
    PhRect_t    *rects = nsnull;
    int         rect_count;

#if 0
    PhRegion_t  my_region;
    PhRect_t    rect = {{0,0},{0,0}};
    int rid;

    rid = gc->rid;

     err = PhRegionQuery(rid, &my_region, &rect, NULL, 0);
	 if (err == -1)
	 {
       PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::ApplyClipping PhRegionQuery returned -1\n"));
	   return;	 
	 }
#endif
	 	   
     /* no offset needed use the normal tile list */
     mClipRegion->GetNativeRegion((void*&)tiles);

     PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::ApplyClipping tiles=<%p>\n", tiles));

     if (tiles != nsnull)
     {
       rects = PhTilesToRects(tiles, &rect_count);
       PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::ApplyClipping Calling PgSetMultiClipping with %d rects\n", rect_count));
       err=PgSetMultiClip(rect_count,rects);
	   if (err == -1)
	   {
		 PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::ApplyClipping Error in PgSetMultiClip probably not enough memory"));
	   	 NS_ASSERTION(0,"nsRenderingContextPh::ApplyClipping Error in PgSetMultiClip probably not enough memory");
	   }
	   
       free(rects);
     }
     else
     {
       PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::ApplyClipping tiles are null\n"));
       //PgSetMultiClip( 0, NULL );
     }
  }
  else
  {
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::ApplyClipping  mClipRegion is NULL"));
    //NS_ASSERTION(mClipRegion,"nsRenderingContextPh::ApplyClipping mClipRegion is NULL");
  }
  
  //PgSetMultiClip( 0, NULL );
}


void nsRenderingContextPh::SetPhLineStyle()
{
  switch( mCurrentLineStyle )
  {
  case nsLineStyle_kSolid:
    PgSetStrokeDash( nsnull, 0, 0x10000 );
    break;

  case nsLineStyle_kDashed:
    PgSetStrokeDash( "\10\4", 2, 0x10000 );
    break;

  case nsLineStyle_kDotted:
    PgSetStrokeDash( "\1", 1, 0x10000 );
    break;

  case nsLineStyle_kNone:
  default:
    break;
  }
}



#if DEBUG

/*******************************************/
static void putshort(FILE *fp, int i)
{
  int c, c1;

  c = ((unsigned int ) i) & 0xff;  c1 = (((unsigned int) i)>>8) & 0xff;
  putc(c, fp);   putc(c1,fp);
}


/*******************************************/
static void putint(FILE *fp, int i)
{
  int c, c1, c2, c3;
  c  = ((unsigned int ) i)      & 0xff;
  c1 = (((unsigned int) i)>>8)  & 0xff;
  c2 = (((unsigned int) i)>>16) & 0xff;
  c3 = (((unsigned int) i)>>24) & 0xff;

  putc(c, fp);   putc(c1,fp);  putc(c2,fp);  putc(c3,fp);
}

/*******************************************/
static void writeBMP24(FILE *fp, unsigned char *pic24,int w,int h)
{
  int   i,j,c,padb;
  unsigned char *pp;
  int xx,yy;	
  unsigned char r,g,b;
  unsigned char p1,p2;
  unsigned long ar,ag,ab;

  padb = (4 - ((w/scale*3) % 4)) & 0x03;  /* # of pad bytes to write at EOscanline */

  for (i=h-1; i>=0; i-=scale) 
  {
    pp = pic24 + DEPTH*x + DEPTH*y*X + (i * X * DEPTH);

    for (j=0; j<w/scale; j++)
    {
	ar=0; ab=0; ag=0;
	for (yy=0;yy<scale;yy++)
	for (xx=0;xx<scale;xx++)
	{
		if (real_depth==24)
		{
			ar+=pp[0+xx*DEPTH-yy*DEPTH*X];
			ag+=pp[1+xx*DEPTH-yy*DEPTH*X];
			ab+=pp[2+xx*DEPTH-yy*DEPTH*X];
		}
		else
		if (real_depth==16)
		{
			p1=pp[0+xx*DEPTH+yy*DEPTH*X];
			p2=pp[1+xx*DEPTH+yy*DEPTH*X];
			ab+= (p1 & 0x1f);
			ag+= ((p1 >> 5) | ((p2 & 7) << 3));
			ar+= (p2 >> 3);
		}
		else
		if (real_depth==15)
		{
			p1=pp[0+xx*DEPTH+yy*DEPTH*X];
			p2=pp[1+xx*DEPTH+yy*DEPTH*X];
			ab+= (p1 & 0x1f);
			ag+= ((p1 >> 5) | ((p2 & 3) << 3));
			ar+= ((p2 & 0x7f) >> 2);
		}
	}
	r = ar/(scale*scale);
	g = ag/(scale*scale);
	b = ab/(scale*scale);
	if (real_depth==24)
	{
		putc(r, fp);
		putc(g, fp);
		putc(b, fp);
	}
	else
	if (real_depth==16)
	{
		putc(b<<3, fp);
		putc(g<<2, fp);
		putc(r<<3, fp);
	}
	else
	if (real_depth==15)
	{
		putc(b<<3, fp);
		putc(g<<3, fp);
		putc(r<<3, fp);
	}
      pp += DEPTH*scale;
    }

    for (j=0; j<padb; j++) putc(0, fp);
  }
}

void do_bmp(char *ptr, int bpl, int W, int H)
{
  char *p;
  FILE *fp;
  int i, nc, nbits, bperlin, cmaplen;
  int w=W;
  int h=H;
  unsigned long aperature=0;
  unsigned char filename[255]="grab.bmp";
  int c;
  static int loop=1;
  char out[255];
  char buf[255];
  char *cp;
  unsigned char *buffer=0;
  int fildes;
  FILE *test;

	X=bpl;
	Y=H;
	DEPTH=24;

	// don't write bmp file if not wanted
	test = fopen ("/dev/shmem/grab","r");
	if (test==0) 
	  return;
	fclose(test);

	p = ptr;
	x=0;
	y=0;
	if (x+w>X || w==0) 
	  w = X-x;
	if (y+h>Y || h==0) 
	  h = Y-y;

    //printf( "aperature 0x%x\n",aperature );
	printf ("X:%d Y:%d DEPTH:%d\n",X,Y,DEPTH);
	printf ("x:%d y:%d w:%d h:%d scale:%d\n",x,y,w,h,scale);

PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::do_bmp X:%d Y:%d DEPTH:%d\n",X,Y,DEPTH));
PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::do_bmp x:%d y:%d w:%d h:%d scale:%d\n",x,y,w,h,scale));

	if (DEPTH!=24 && DEPTH!=16 && DEPTH!=15)
	{
	  printf("Depth must be 15,16 or 24 for now.\n");
	  exit(0);
	}

	real_depth=DEPTH;
	DEPTH = (DEPTH+1)/8;

	if (loop)
	{
		cp = strstr(filename,".");
		if (cp==0)
			sprintf (buf,"%s%d",filename,loop++);
		else
		{
			*cp = 0;
			sprintf (buf,"%s%d.%s",filename,loop++,cp+1);
			*cp = '.';
		}
	}
	else
	{
		strcpy(buf,filename);
	}

	printf ("bmp file: %s\n",buf);
    PR_LOG(PhGfxLog, PR_LOG_DEBUG, ("nsRenderingContextPh::do_bmp bmp file: %s\n",buf));

	fp = fopen (buf,"w");

	{
   nbits = 24;
   cmaplen = 0;
   nc = 0;
   bperlin = ((w * nbits + 31) / 32) * 4;   /* # bytes written per line */

  putc('B', fp);  putc('M', fp);           /* BMP file magic number */

  /* compute filesize and write it */
  i = 14 +                /* size of bitmap file header */
      40 +                /* size of bitmap info header */
      (nc * 4) +          /* size of colormap */
      bperlin * h;        /* size of image data */

  putint(fp, i);
  putshort(fp, 0);        /* reserved1 */
  putshort(fp, 0);        /* reserved2 */
  putint(fp, 14 + 40 + (nc * 4));  /* offset from BOfile to BObitmap */

  putint(fp, 40);         /* biSize: size of bitmap info header */
  putint(fp, w/scale);          /* biWidth */
  putint(fp, h/scale);          /* biHeight */
  putshort(fp, 1);        /* biPlanes:  must be '1' */
  putshort(fp, nbits);    /* biBitCount: 1,4,8, or 24 */
#define BI_RGB 0
  putint(fp, BI_RGB);     /* biCompression:  BI_RGB, BI_RLE8 or BI_RLE4 */
  putint(fp, bperlin*h);  /* biSizeImage:  size of raw image data */
  putint(fp, 75 * 39);    /* biXPelsPerMeter: (75dpi * 39" per meter) */
  putint(fp, 75 * 39);    /* biYPelsPerMeter: (75dpi * 39" per meter) */
  putint(fp, nc);         /* biClrUsed: # of colors used in cmap */
  putint(fp, nc);         /* biClrImportant: same as above */

  if (buffer)
	writeBMP24(fp,buffer,w,h);
  else
	writeBMP24(fp,p,w,h);
	}
	fclose(fp);
}

#endif


