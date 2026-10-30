/*
 * SynthLib - common library for synthesizer editor applications.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __SYNTHLIB_AU_VIEW_FACTORY_H__
#define __SYNTHLIB_AU_VIEW_FACTORY_H__

// The one Obj-C class a product supplies itself. An Audio Unit names its view factory by class, and
// hosts look that name up with -[NSBundle classNamed:], which only finds classes compiled into that
// bundle: it returns nil for one made at run time. So this cannot be a run-time class like the
// others (synthlibRuntimeClass.h), and it cannot be in SynthLib, as the name must differ per plug-in.
// A product's build expands this once, in its own source, with its own name.

#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>
#import <Cocoa/Cocoa.h>

NSString * synthlib_au_view_description(void);
NSView * synthlib_au_view_create(AudioUnit inAU, NSSize inPreferredSize);
void synthlib_au_set_view_class_name(const char * name);

#define SYNTHLIB_AU_VIEW_FACTORY(className)                                          \
   @interface className : NSObject<AUCocoaUIBase>                                    \
   @end                                                                              \
   @implementation className                                                         \
   - (unsigned)interfaceVersion {                                                    \
       return 0;                                                                     \
   }                                                                                 \
   - (NSString *)description {                                                       \
       return synthlib_au_view_description();                                        \
   }                                                                                 \
   - (NSView *)uiViewForAudioUnit:(AudioUnit)inAU withSize:(NSSize)inPreferredSize { \
       return synthlib_au_view_create(inAU, inPreferredSize);                        \
   }                                                                                 \
   @end                                                                              \
   __attribute__((constructor)) static void synthlib_au_view_register(void) {        \
       synthlib_au_set_view_class_name(# className);                                 \
   }

#endif // __SYNTHLIB_AU_VIEW_FACTORY_H__
