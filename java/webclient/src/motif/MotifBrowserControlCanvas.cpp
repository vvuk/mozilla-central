/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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


/*
 * MotifBrowserControlCanvas.cpp
 */

#include <jni.h>
#include "MotifBrowserControlCanvas.h"

#include <X11/Xlib.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include "gtkmozilla.h"

#include "nsIDOMDocument.h"
#include <dlfcn.h>

extern "C" void NS_SetupRegistry();

extern "C" {

/*
 * Class:     org_mozilla_webclient_motif_MotifBrowserControlCanvas
 * Method:    createTopLevelWindow
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_mozilla_webclient_motif_MotifBrowserControlCanvas_createTopLevelWindow
(JNIEnv * env, jobject obj) {
    static GtkWidget *window = NULL;

    // PENDING(mark): This is a hack needed in order to get those error
    // messages about:
    /***************************************************
     *nsComponentManager: Load(/disk4/mozilla/mozilla/dist/bin/components/librdf.so) 
     *FAILED with error: libsunwjdga.so: cannot open shared object file: 
     *No such file or directory
     ****************************************************/
    // But the weird thing is, libhistory.so isn't linked with libsunwjdga.so.
    // In fact, libsunwjdga.so doesn't even exist anywhere! I know it's
    // a JDK library, but I think it is only supposed to get used on
    // Solaris, not Linux. And why the hell is nsComponentManager trying
    // to open this JDK library anyways? I need to try and get my stuff
    // building on Solaris. Also, I will have to scan the Linux 
    // JDK1.2 sources and figure out what's going on. In the meantime....
    //
    // -Mark
    void * dll;

    dll = dlopen("components/libhistory.so", RTLD_NOW | RTLD_GLOBAL);
    if (!dll) {
        printf("Got Error: %s\n", dlerror());
    }
    dll = dlopen("components/librdf.so", RTLD_NOW | RTLD_GLOBAL);
    if (!dll) {
        printf("Got Error: %s\n", dlerror());
    }

    NS_SetupRegistry();
    
    /* Initialise GTK */
    gtk_set_locale ();
    
    gtk_init (0, NULL);
    
    gdk_rgb_init();
    
    window = gtk_window_new (GTK_WINDOW_POPUP);
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 300);
    gtk_window_set_title(GTK_WINDOW(window), "Simple browser");

    return (jint) window;
}

/*
 * Class:     org_mozilla_webclient_motif_MotifBrowserControlCanvas
 * Method:    createContainerWindow
 * Signature: (III)I
 */
JNIEXPORT jint JNICALL Java_org_mozilla_webclient_motif_MotifBrowserControlCanvas_createContainerWindow
    (JNIEnv * env, jobject obj, jint parent, jint screenWidth, jint screenHeight) {
    GtkWidget * window = (GtkWidget *) parent;
    GtkWidget * vbox;
    GtkWidget * mozilla;
    
    vbox = gtk_vbox_new (FALSE, 5);
    gtk_container_add (GTK_CONTAINER (window), vbox);
    gtk_widget_show(vbox);
    
    mozilla = gtk_mozilla_new();
    gtk_box_pack_start (GTK_BOX (vbox), mozilla, TRUE, TRUE, 0);
    
    // HACK: javaMake sure this window doesn't appear onscreen!!!!
    gtk_widget_set_uposition(window, screenWidth + 20, screenHeight + 20);
    gtk_widget_show(mozilla);
    
    gtk_widget_show(window);
    
    //gtk_main();

    return (jint) mozilla;
}

int getWinID(GtkWidget * gtkWidgetPtr) {
    //GdkWindow * gdkWindow = gtk_widget_get_parent_window(gtkWidgetPtr);
    GdkWindow * gdkWindow = gtkWidgetPtr->window;
    int gtkwinid = GDK_WINDOW_XWINDOW(gdkWindow);
    
    return gtkwinid;
}

/*
 * Class:     org_mozilla_webclient_motif_MotifBrowserControlCanvas
 * Method:    getGTKWinID
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_org_mozilla_webclient_motif_MotifBrowserControlCanvas_getGTKWinID
(JNIEnv * env, jobject obj, jint gtkWinPtr) {
    GtkWidget * gtkWidgetPtr = (GtkWidget *) gtkWinPtr;

    return getWinID(gtkWidgetPtr);
}


/*
 * Class:     org_mozilla_webclient_motif_MotifBrowserControlCanvas
 * Method:    reparentWindow
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_org_mozilla_webclient_motif_MotifBrowserControlCanvas_reparentWindow (JNIEnv * env, jobject obj, jint childID, jint parentID) {
    XReparentWindow(GDK_DISPLAY(), childID, parentID, 0, 0);
}

/*
 * Class:     org_mozilla_webclient_motif_MotifBrowserControlCanvas
 * Method:    processEvents
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_mozilla_webclient_motif_MotifBrowserControlCanvas_processEvents
    (JNIEnv * env, jobject obj) {
    //printf("process events....\n");
    //processEventLoopIntelligently();
}

/*
 * Class:     org_mozilla_webclient_motif_MotifBrowserControlCanvas
 * Method:    setGTKWindowSize
 * Signature: (III)V
 */
JNIEXPORT void JNICALL Java_org_mozilla_webclient_motif_MotifBrowserControlCanvas_setGTKWindowSize
    (JNIEnv * env, jobject obj, jint gtkWinPtr, jint width, jint height) {
    if (gtkWinPtr != 0) {
        GtkWidget * gtkWidgetPtr = (GtkWidget *) gtkWinPtr;

        if (gtkWidgetPtr) {
            gtk_widget_set_usize(gtkWidgetPtr, width, height);
        }
    }
}


} // End extern "C"




