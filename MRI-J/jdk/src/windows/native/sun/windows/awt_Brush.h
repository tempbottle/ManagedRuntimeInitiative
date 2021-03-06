/*
 * Copyright 1996-2002 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

#ifndef AWT_BRUSH_H
#define AWT_BRUSH_H

#include "awt_GDIObject.h"
#include "GDIHashtable.h"

/*
 * An AwtBrush is a cached Windows brush.
 */
class AwtBrush : public AwtGDIObject {
public:
    /*
     * Get a GDI object from its respective cache.  If it doesn't exist
     * it gets created, otherwise its reference count gets bumped.
     */
    static AwtBrush* Get(COLORREF color);

    // Delete an AwtBrush, called by Hashtable.clear().
    static void DeleteAwtBrush(void* pBrush);

protected:
    /*
     * Decrement the reference count of a cached GDI object.  When it hits
     * zero, notify the cache that the object can be safely removed.
     * The cache will eventually delete the GDI object and this wrapper.
     */
    virtual void ReleaseInCache();

private:
    AwtBrush(COLORREF color);
    ~AwtBrush() {}

    static GDIHashtable cache;
};

#endif // AWT_BRUSH_H
