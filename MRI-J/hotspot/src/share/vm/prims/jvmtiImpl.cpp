/*
 * Copyright 2003-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
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
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.



#include "collectedHeap.hpp"
#include "interfaceSupport.hpp"
#include "handles.hpp"
#include "javaClasses.hpp"
#include "jvmtiAgentThread.hpp"
#include "jvmtiEnvBase.hpp"
#include "jvmtiExport.hpp"
#include "jvmtiImpl.hpp"
#include "jvmtiThreadState.hpp"
#include "mutexLocker.hpp"
#include "oopFactory.hpp"
#include "safepoint.hpp"
#include "tickProfiler.hpp"
#include "utf8.hpp"
#include "vframe.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#endif // AZ_PROXIED

// Machine generated file, must be last
#include "jvmtiEnv.hpp"

GrowableArray<JvmtiRawMonitor*> *JvmtiPendingMonitors::_monitors = new (ResourceObj::C_HEAP) GrowableArray<JvmtiRawMonitor*>(1,true);

void JvmtiPendingMonitors::transition_raw_monitors() {
  assert((Threads::number_of_threads()==1),
         "Java thread has not created yet or more than one java thread \
is running. Raw monitor transition will not work");
  JavaThread *current_java_thread = JavaThread::current();
  {
ThreadBlockInVM __tbivm(current_java_thread,"JVMTI transition raw monitors");
    for(int i=0; i< count(); i++) {
      JvmtiRawMonitor *rmonitor = monitors()->at(i);
Unimplemented();//FIXME
      // rmonitor->lock_recursively();
      //int r = rmonitor->raw_enter(current_java_thread, true, false);
      //assert(r == ObjectMonitor::OM_OK, "raw_enter should have worked");
    }
  }
  // pending monitors are converted to real monitor so delete them all.
  dispose();
}

//
// class JvmtiAgentThread
//
// JavaThread used to wrap a thread started by an agent
// using the JVMTI method RunAgentThread.
//

static bool is_start_function_remote(const char*name){
    // HACK HACK HACK ... SIGH ... to determine whether the start function is on
    // the proxy side we check the callers to see whether we came from
    // jvmti_tie_RunAgentThread .
    // Don't want to modify jvmti.h and the xml and xsl
    // files to pass in another parameter indicating whether this function is remote.

char buf[25];
    int offset;
    size_t size;
    bool found = false;
    frame fr = os::current_frame();

guarantee(fr.pc(),"cannot determine if agent is remote or local");

    int count = 0;
    while (count++ < 10) {
      fr = os::get_sender_for_C_frame(&fr);
      if (os::is_first_C_frame(&fr)) break;

      found = os::dll_address_to_function_name(fr.pc(), buf, sizeof(buf), &offset, &size);
      if (found) {
        if (strcmp(buf, name) == 0) {
          return true;
        }
      } else {
        // We cannot have intervening unrecognizable non-VM frames between
        // jvmti_tie_RunAgentThread and here. So return false.
        return false;
      }
    }

    return false;
}

JvmtiAgentThread::JvmtiAgentThread(JvmtiEnv* env, jvmtiStartFunction start_fn, const void *start_arg)
    : JavaThread(start_function_wrapper) {
    _env = env;
    _start_fn = start_fn;
    _start_arg = start_arg;
    _start_function_is_remote = is_start_function_remote("jvmti_tie_RunAgentThread");
}

void
JvmtiAgentThread::start_function_wrapper(JavaThread *thread, TRAPS) {
    // It is expected that any Agent threads will be created as
    // Java Threads.  If this is the case, notification of the creation
    // of the thread is given in JavaThread::thread_main().
    assert(thread->is_Java_thread(), "debugger thread should be a Java Thread");
    assert(thread == JavaThread::current(), "sanity check");
  
    JvmtiAgentThread *dthread = (JvmtiAgentThread *)thread;
    dthread->call_start_function();
}

void
JvmtiAgentThread::call_start_function() {
    ThreadToNativeFromVM transition(this);

    if (_start_function_is_remote) {
#ifndef AZ_PROXIED
      // TODO: remote functions not supported in unproxied builds.  Need to verify that
      // _start_function is_remote could never be true.
      Unimplemented();
#else // AZ_PROXIED:
      JNIEnv* env = jni_environment();
if(_env!=NULL){
        proxy_invoke_remote_jvmti_thread_function(_env->jvmti_external(), 
                                                  jni_environment(),
                                                  (void*)_start_fn, _start_arg);
      }
#endif // AZ_PROXIED
    } else {
if(_env!=NULL){
        _start_fn(_env->jvmti_external(), jni_environment(), (void*)_start_arg);
      }
    }
}

//
// class JvmtiUtil
//

ResourceArea* JvmtiUtil::_single_threaded_resource_area = NULL;

ResourceArea* JvmtiUtil::single_threaded_resource_area() {
  if (_single_threaded_resource_area == NULL) {
    // lazily create the single threaded resource area
    // pick a size which is not a standard since the pools don't exist yet
    _single_threaded_resource_area = new ResourceArea(Chunk::non_pool_size);
  }
  return _single_threaded_resource_area;
}

//
// class JvmtiTrace
//
// Support for JVMTI tracing code
//
// ------------
// Usage:
//    -XX:TraceJVMTI=DESC,DESC,DESC
//
//    DESC is   DOMAIN ACTION KIND
//
//    DOMAIN is function name
//              event name
//              "all" (all functions and events)
//              "func" (all functions except boring)
//              "allfunc" (all functions)
//              "event" (all events)
//              "ec" (event controller)
//
//    ACTION is "+" (add)
//              "-" (remove)
//
//    KIND is
//     for func
//              "i" (input params)
//              "e" (error returns)
//              "o" (output)
//     for event
//              "t" (event triggered aka posted)
//              "s" (event sent)
//
// Example:
//            -XX:TraceJVMTI=ec+,GetCallerFrame+ie,Breakpoint+s

#ifdef JVMTI_TRACE

bool JvmtiTrace::_initialized = false;
bool JvmtiTrace::_on = false;
bool JvmtiTrace::_trace_event_controller = false;

void JvmtiTrace::initialize() {
  if (_initialized) {
    return;
  }
  SafeResourceMark rm;
  
  const char *very_end;
  const char *curr;
  if (strlen(TraceJVMTI)) {
    curr = TraceJVMTI;
  } else {
    curr = "";  // hack in fixed tracing here
  }
  very_end = curr + strlen(curr);
  while (curr < very_end) {
    const char *curr_end = strchr(curr, ',');
    if (curr_end == NULL) {
      curr_end = very_end;
    }
    const char *op_pos = strchr(curr, '+');
    const char *minus_pos = strchr(curr, '-');
    if (minus_pos != NULL && (minus_pos < op_pos || op_pos == NULL)) {
      op_pos = minus_pos;
    }
    char op;
    const char *flags = op_pos + 1;
    const char *flags_end = curr_end;
    if (op_pos == NULL || op_pos > curr_end) {
      flags = "ies";
      flags_end = flags + strlen(flags);
      op_pos = curr_end;
      op = '+';
    } else {
      op = *op_pos;
    }
    jbyte bits = 0;
    for (; flags < flags_end; ++flags) {
      switch (*flags) {
      case 'i': 
        bits |= SHOW_IN;
        break;
      case 'I': 
        bits |= SHOW_IN_DETAIL;
        break;
      case 'e': 
        bits |= SHOW_ERROR;
        break;
      case 'o': 
        bits |= SHOW_OUT;
        break;
      case 'O': 
        bits |= SHOW_OUT_DETAIL;
        break;
      case 't': 
        bits |= SHOW_EVENT_TRIGGER;
        break;
      case 's': 
        bits |= SHOW_EVENT_SENT;
        break;
      default:
        tty->print_cr("Invalid trace flag '%c'", *flags);
        break;
      }
    }
    const int FUNC = 1;
    const int EXCLUDE  = 2;
    const int ALL_FUNC = 4;
    const int EVENT = 8;
    const int ALL_EVENT = 16;
    int domain = 0;
    size_t len = op_pos - curr;
    if (op_pos == curr) {
      domain = ALL_FUNC | FUNC | ALL_EVENT | EVENT | EXCLUDE;
    } else if (len==3 && strncmp(curr, "all", 3)==0) {
      domain = ALL_FUNC | FUNC | ALL_EVENT | EVENT;
    } else if (len==7 && strncmp(curr, "allfunc", 7)==0) {
      domain = ALL_FUNC | FUNC;
    } else if (len==4 && strncmp(curr, "func", 4)==0) {
      domain = ALL_FUNC | FUNC | EXCLUDE;
    } else if (len==8 && strncmp(curr, "allevent", 8)==0) {
      domain = ALL_EVENT | EVENT;
    } else if (len==5 && strncmp(curr, "event", 5)==0) {
      domain = ALL_EVENT | EVENT;
    } else if (len==2 && strncmp(curr, "ec", 2)==0) {
      _trace_event_controller = true;
      tty->print_cr("JVMTI Tracing the event controller");
    } else {
      domain = FUNC | EVENT;  // go searching
    }

    int exclude_index = 0;
    if (domain & FUNC) {
      if (domain & ALL_FUNC) {
        if (domain & EXCLUDE) {
          tty->print("JVMTI Tracing all significant functions");
        } else {
          tty->print_cr("JVMTI Tracing all functions");
        }
      }
      for (int i = 0; i <= _max_function_index; ++i) {
        if (domain & EXCLUDE && i == _exclude_functions[exclude_index]) {
          ++exclude_index;
        } else {
          bool do_op = false;
          if (domain & ALL_FUNC) {
            do_op = true;
          } else {
            const char *fname = function_name(i);
            if (fname != NULL) {
              size_t fnlen = strlen(fname);
              if (len==fnlen && strncmp(curr, fname, fnlen)==0) {
                tty->print_cr("JVMTI Tracing the function: %s", fname);
                do_op = true;
              }
            }
          }
          if (do_op) {
            if (op == '+') {
              _trace_flags[i] |= bits;
            } else {
              _trace_flags[i] &= ~bits;
            }
            _on = true;
          }
        }
      }
    }
    if (domain & EVENT) {
      if (domain & ALL_EVENT) {
        tty->print_cr("JVMTI Tracing all events");
      }
      for (int i = 0; i <= _max_event_index; ++i) {
        bool do_op = false;
        if (domain & ALL_EVENT) {
          do_op = true;
        } else {
          const char *ename = event_name(i);
          if (ename != NULL) {
            size_t evtlen = strlen(ename);
            if (len==evtlen && strncmp(curr, ename, evtlen)==0) {
              tty->print_cr("JVMTI Tracing the event: %s", ename);
              do_op = true;
            }
          }
        }
        if (do_op) {
          if (op == '+') {
            _event_trace_flags[i] |= bits;
          } else {
            _event_trace_flags[i] &= ~bits;
          }
          _on = true;
        }
      }
    }
    if (!_on && (domain & (FUNC|EVENT))) {
      tty->print_cr("JVMTI Trace domain not found");
    }
    curr = curr_end + 1;
  }
  _initialized = true;
}


void JvmtiTrace::shutdown() {
  int i;
  _on = false;
  _trace_event_controller = false;
  for (i = 0; i <= _max_function_index; ++i) {
    _trace_flags[i] = 0;
  }
  for (i = 0; i <= _max_event_index; ++i) {
    _event_trace_flags[i] = 0;
  }
}


const char* JvmtiTrace::enum_name(const char** names, const jint* values, jint value) {
  for (int index = 0; names[index] != 0; ++index) {
    if (values[index] == value) {
      return names[index];
    }
  }
  return "*INVALID-ENUM-VALUE*";
}


// return a valid string no matter what state the thread is in
const char *JvmtiTrace::safe_get_thread_name(Thread *thread) {
  if (thread == NULL) {
    return "NULL";
  }
  if (!thread->is_Java_thread()) {
    return thread->name();
  }
  JavaThread *java_thread = (JavaThread *)thread;
  oop threadObj = java_thread->threadObj();
  if (threadObj == NULL) {
    return "NULL";
  }
  typeArrayOop name = java_lang_Thread::name(threadObj);
  if (name == NULL) {
    return "<NOT FILLED IN>";
  }
  return UNICODE::as_utf8((jchar*) name->base(T_CHAR), name->length());
}
    

// return the name of the current thread
const char *JvmtiTrace::safe_get_current_thread_name() {
  if (JvmtiEnv::is_vm_live()) {
    return JvmtiTrace::safe_get_thread_name(Thread::current());
  } else {
    return "VM not live";
  }
}

// return a valid string no matter what the state of k_mirror
const char * JvmtiTrace::get_class_name(oop k_mirror) {
  if (java_lang_Class::is_primitive(k_mirror)) {
    return "primitive";
  }
  klassOop k_oop = java_lang_Class::as_klassOop(k_mirror);
  if (k_oop == NULL) {
    return "INVALID";
  }
  return Klass::cast(k_oop)->external_name();
}

#endif /*JVMTI_TRACE */

//
// class GrowableCache - private methods
//

void GrowableCache::recache() {
  int len = _elements->length();

  FREE_C_HEAP_ARRAY(address, _cache);
  _cache = NEW_C_HEAP_ARRAY(address,len+1);

  for (int i=0; i<len; i++) {
    _cache[i] = _elements->at(i)->getCacheValue();
    //
    // The cache entry has gone bad. Without a valid frame pointer
    // value, the entry is useless so we simply delete it in product
    // mode. The call to remove() will rebuild the cache again
    // without the bad entry.
    //
    if (_cache[i] == NULL) {
      assert(false, "cannot recache NULL elements");
      remove(i);
      return;
    }
  }
  _cache[len] = NULL;

  _listener_fun(_this_obj,_cache);
}

bool GrowableCache::equals(void* v, GrowableElement *e2) {
  GrowableElement *e1 = (GrowableElement *) v;
  assert(e1 != NULL, "e1 != NULL");
  assert(e2 != NULL, "e2 != NULL");

  return e1->equals(e2);
}

//
// class GrowableCache - public methods
//

GrowableCache::GrowableCache() {
  _this_obj       = NULL;
  _listener_fun   = NULL;    
  _elements       = NULL;
  _cache          = NULL;
}

GrowableCache::~GrowableCache() {
  clear();
  delete _elements;
  FREE_C_HEAP_ARRAY(address, _cache);
}

void GrowableCache::initialize(void *this_obj, void listener_fun(void *, address*) ) {
  _this_obj       = this_obj;
  _listener_fun   = listener_fun;    
  _elements       = new (ResourceObj::C_HEAP) GrowableArray<GrowableElement*>(5,true);
  recache();
}

// number of elements in the collection
int GrowableCache::length() { 
  return _elements->length(); 
}

// get the value of the index element in the collection
GrowableElement* GrowableCache::at(int index) {
  GrowableElement *e = (GrowableElement *) _elements->at(index);
  assert(e != NULL, "e != NULL");
  return e;
}
 
int GrowableCache::find(GrowableElement* e) {
  return _elements->find(e, GrowableCache::equals);
}

// append a copy of the element to the end of the collection
void GrowableCache::append(GrowableElement* e) {
  GrowableElement *new_e = e->clone();
  _elements->append(new_e);
  recache();
}

// insert a copy of the element using lessthan()
void GrowableCache::insert(GrowableElement* e) {
  GrowableElement *new_e = e->clone();
  _elements->append(new_e);

  int n = length()-2;
  for (int i=n; i>=0; i--) {
    GrowableElement *e1 = _elements->at(i);
    GrowableElement *e2 = _elements->at(i+1);
    if (e2->lessThan(e1)) {
      _elements->at_put(i+1, e1);
      _elements->at_put(i,   e2);
    }
  }

  recache();
}

// remove the element at index
void GrowableCache::remove (int index) {
  GrowableElement *e = _elements->at(index);
  assert(e != NULL, "e != NULL");
  _elements->remove(e);
  delete e;
  recache();
}

// clear out all elements, release all heap space and
// let our listener know that things have changed.
void GrowableCache::clear() {
  int len = _elements->length();
  for (int i=0; i<len; i++) {
    delete _elements->at(i);
  }
  _elements->clear();
  recache();
}

void GrowableCache::oops_do(OopClosure* f) {
  int len = _elements->length();
  for (int i=0; i<len; i++) {
    GrowableElement *e = _elements->at(i);
    e->oops_do(f);
  }
}

void GrowableCache::gc_epilogue() {
  int len = _elements->length();
  // recompute the new cache value after GC
  for (int i=0; i<len; i++) {
    _cache[i] = _elements->at(i)->getCacheValue();
  }
}


//
// class JvmtiRawMonitor
//

JvmtiRawMonitor::JvmtiRawMonitor(objectRef ref, const char *name) 
// FIXME - why does main-dev-x86 branch have an objectRef argument?  
//         Doesn't seem to match up with any constructor...
//   : ObjectMonitor(ref) 
{
#ifdef ASSERT
  _name = strcpy(NEW_C_HEAP_ARRAY(char, strlen(name) + 1), name);
#else
  _name = NULL;
#endif
  _magic = JVMTI_RM_MAGIC;
}

JvmtiRawMonitor::~JvmtiRawMonitor() {
#ifdef ASSERT
  FreeHeap(_name);
#endif
  _magic = 0;
}


//
// class JvmtiBreakpoint
//

JvmtiBreakpoint::JvmtiBreakpoint() {
_method=nullRef;
  _bci    = 0;
#ifdef CHECK_UNHANDLED_OOPS
  // This one is always allocated with new, but check it just in case.
  Thread *thread = Thread::current();
  if (thread->is_in_stack((address)&_method)) {
    thread->allow_unhandled_oop((oop*)&_method);
  }
#endif // CHECK_UNHANDLED_OOPS
}

JvmtiBreakpoint::JvmtiBreakpoint(methodOop m_method, jlocation location) {
POISON_AND_STORE_REF(&_method,methodRef(m_method));
assert(_method.not_null(),"_method != NULL");
  _bci           = (int) location;  
#ifdef CHECK_UNHANDLED_OOPS
  // Could be allocated with new and wouldn't be on the unhandled oop list.
  Thread *thread = Thread::current();
  if (thread->is_in_stack((address)&_method)) {
    thread->allow_unhandled_oop(&_method);
  }
#endif // CHECK_UNHANDLED_OOPS

  assert(_bci >= 0, "_bci >= 0"); 
}

void JvmtiBreakpoint::copy(JvmtiBreakpoint& bp) {
POISON_AND_STORE_REF(&_method,bp._method);
  _bci      = bp._bci;
}

bool JvmtiBreakpoint::lessThan(JvmtiBreakpoint& bp) {
  Unimplemented();
  return false;
}

bool JvmtiBreakpoint::equals(JvmtiBreakpoint& bp) {
return method()==bp.method()
    &&   _bci     == bp._bci;
}

bool JvmtiBreakpoint::is_valid() {
return method()!=NULL&&
         _bci >= 0;
}

address JvmtiBreakpoint::getBcp() {
  address bcp;
methodOop moop=NULL;
  constMethodOop cmoop = NULL;
  Thread* thread = Thread::current();

  if (UseLVBs && thread->is_gc_mode()) {
    if (UseGenPauselessGC) {
      moop  = (methodOop) GPGC_Collector::remap_only(&_method).as_oop();
      cmoop = (constMethodOop) GPGC_Collector::remap_only(moop->adr_constMethod()).as_oop();
    } else {
      ShouldNotReachHere();
    }

    bcp = cmoop->code_base() + _bci;
    assert((moop->is_native() && bcp == cmoop->code_base())|| cmoop->contains(bcp), "bcp doesn't belong to this method");
  } else {
    bcp = method()->bcp_from(_bci);
  }

  return bcp;
}

void JvmtiBreakpoint::each_method_version_do(method_action meth_act) {
  methodOop moop = method();
  (moop->*meth_act)(_bci);

  // add/remove breakpoint to/from versions of the method that
  // are EMCP. Directly or transitively obsolete methods are
  // not saved in the PreviousVersionInfo.
  Thread *thread = Thread::current();
instanceKlassHandle ikh=instanceKlassHandle(thread,moop->method_holder());
symbolOop m_name=moop->name();
symbolOop m_signature=moop->signature();

  { 
    ResourceMark rm(thread);
    // PreviousVersionInfo objects returned via PreviousVersionWalker
    // contain a GrowableArray of handles. We have to clean up the
    // GrowableArray _after_ the PreviousVersionWalker destructor
    // has destroyed the handles.
    {
      // search previous versions if they exist
      PreviousVersionWalker pvw((instanceKlass *)ikh()->klass_part());
      for (PreviousVersionInfo * pv_info = pvw.next_previous_version();
           pv_info != NULL; pv_info = pvw.next_previous_version()) {
        GrowableArray<methodHandle>* methods =
          pv_info->prev_EMCP_method_handles();

        if (methods == NULL) {
          // We have run into a PreviousVersion generation where
          // all methods were made obsolete during that generation's
          // RedefineClasses() operation. At the time of that
          // operation, all EMCP methods were flushed so we don't
          // have to go back any further.
          //
          // A NULL methods array is different than an empty methods
          // array. We cannot infer any optimizations about older
          // generations from an empty methods array for the current
          // generation.
          break;
        }

        for (int i = methods->length() - 1; i >= 0; i--) {
          methodHandle method = methods->at(i);
          if (method->name() == m_name && method->signature() == m_signature) {
            // FIXME?
            // RC_TRACE(0x00000800, ("%sing breakpoint in %s(%s)",
            //   meth_act == &methodOopDesc::set_breakpoint ? "sett" : "clear",
            //   method->name()->as_C_string(),
            //   method->signature()->as_C_string()));
            assert(!method->is_obsolete(), "only EMCP methods here");

            ((methodOopDesc*)method()->*meth_act)(_bci);
            break;
          }
        }
      }
    } // pvw is cleaned up
  } // rm is cleaned up
}

void JvmtiBreakpoint::set() {
  each_method_version_do(&methodOopDesc::set_breakpoint);
}

void JvmtiBreakpoint::clear() {
  each_method_version_do(&methodOopDesc::clear_breakpoint);
}

void JvmtiBreakpoint::print() {
#ifndef PRODUCT
methodOop moop=method();
const char*class_name=(moop==NULL)?"NULL":moop->klass_name()->as_C_string();
const char*method_name=(moop==NULL)?"NULL":moop->name()->as_C_string();

  tty->print("Breakpoint(%s,%s,%d,%p)",class_name, method_name, _bci, getBcp());
#endif
}


//
// class VM_ChangeBreakpoints
//
// Modify the Breakpoints data structure at a safepoint
//

void VM_ChangeBreakpoints::doit() {
  switch (_operation) {
  case SET_BREAKPOINT:
    _breakpoints->set_at_safepoint(*_bp);
    break;
  case CLEAR_BREAKPOINT:
    _breakpoints->clear_at_safepoint(*_bp);
    break;
  case CLEAR_ALL_BREAKPOINT:
    _breakpoints->clearall_at_safepoint();
    break;
  default:
    assert(false, "Unknown operation");
  }
}

void VM_ChangeBreakpoints::oops_do(OopClosure* f) {
  // This operation keeps breakpoints alive
  if (_breakpoints != NULL) {
    _breakpoints->oops_do(f);
  }
  if (_bp != NULL) {
    _bp->oops_do(f);
  }
}

//
// class JvmtiBreakpoints 
//
// a JVMTI internal collection of JvmtiBreakpoint
//

JvmtiBreakpoints::JvmtiBreakpoints(void listener_fun(void *,address *)) {
  _bps.initialize(this,listener_fun);
}

JvmtiBreakpoints:: ~JvmtiBreakpoints() {}

void  JvmtiBreakpoints::oops_do(OopClosure* f) {  
  _bps.oops_do(f);
} 

void  JvmtiBreakpoints::gc_epilogue() {  
  _bps.gc_epilogue();
} 

void  JvmtiBreakpoints::print() {
#ifndef PRODUCT
  ResourceMark rm;

  int n = _bps.length();
  for (int i=0; i<n; i++) {
    JvmtiBreakpoint& bp = _bps.at(i);
    tty->print("%d: ", i);
    bp.print();
    tty->cr();
  }
#endif
}


void JvmtiBreakpoints::set_at_safepoint(JvmtiBreakpoint& bp) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");

  int i = _bps.find(bp);
  if (i == -1) { 
    _bps.append(bp);
    bp.set();
  }
}

void JvmtiBreakpoints::clear_at_safepoint(JvmtiBreakpoint& bp) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");

  int i = _bps.find(bp);
  if (i != -1) {
    _bps.remove(i);
    bp.clear();
  }
}

void JvmtiBreakpoints::clearall_at_safepoint() {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");

  int len = _bps.length();
  for (int i=0; i<len; i++) {
    _bps.at(i).clear();
  }
  _bps.clear();
}
 
int JvmtiBreakpoints::length() { return _bps.length(); }

int JvmtiBreakpoints::set(JvmtiBreakpoint& bp) {
  if ( _bps.find(bp) != -1) {
     return JVMTI_ERROR_DUPLICATE;
  }
  VM_ChangeBreakpoints set_breakpoint(this,VM_ChangeBreakpoints::SET_BREAKPOINT, &bp);
  VMThread::execute(&set_breakpoint);
  return JVMTI_ERROR_NONE;
}

int JvmtiBreakpoints::clear(JvmtiBreakpoint& bp) {
  if ( _bps.find(bp) == -1) {
     return JVMTI_ERROR_NOT_FOUND;
  }

  VM_ChangeBreakpoints clear_breakpoint(this,VM_ChangeBreakpoints::CLEAR_BREAKPOINT, &bp);
  VMThread::execute(&clear_breakpoint);
  return JVMTI_ERROR_NONE;
}

void JvmtiBreakpoints::clearall_in_class_at_safepoint(klassOop klass) {
  bool changed = true;
  // We are going to run thru the list of bkpts
  // and delete some.  This deletion probably alters
  // the list in some implementation defined way such
  // that when we delete entry i, the next entry might
  // no longer be at i+1.  To be safe, each time we delete
  // an entry, we'll just start again from the beginning.
  // We'll stop when we make a pass thru the whole list without
  // deleting anything.
  while (changed) {
    int len = _bps.length();
    changed = false;
    for (int i = 0; i < len; i++) {
      JvmtiBreakpoint& bp = _bps.at(i);
      if (bp.method()->method_holder() == klass) {
        bp.clear();
        _bps.remove(i);
        // This changed 'i' so we have to start over.
        changed = true;
        break;
      }
    }
  }
}

void JvmtiBreakpoints::clearall() {
  VM_ChangeBreakpoints clearall_breakpoint(this,VM_ChangeBreakpoints::CLEAR_ALL_BREAKPOINT);
  VMThread::execute(&clearall_breakpoint);
}

//
// class JvmtiCurrentBreakpoints
// 

JvmtiBreakpoints *JvmtiCurrentBreakpoints::_jvmti_breakpoints  = NULL;
address *         JvmtiCurrentBreakpoints::_breakpoint_list    = NULL;


JvmtiBreakpoints& JvmtiCurrentBreakpoints::get_jvmti_breakpoints() {
  if (_jvmti_breakpoints != NULL) return (*_jvmti_breakpoints);
  _jvmti_breakpoints = new JvmtiBreakpoints(listener_fun);
  assert(_jvmti_breakpoints != NULL, "_jvmti_breakpoints != NULL");
  return (*_jvmti_breakpoints);
}

void  JvmtiCurrentBreakpoints::listener_fun(void *this_obj, address *cache) { 
  JvmtiBreakpoints *this_jvmti = (JvmtiBreakpoints *) this_obj;
  assert(this_jvmti != NULL, "this_jvmti != NULL");

  debug_only(int n = this_jvmti->length(););
  assert(cache[n] == NULL, "cache must be NULL terminated");

  set_breakpoint_list(cache);
}


void JvmtiCurrentBreakpoints::oops_do(OopClosure* f) { 
  if (_jvmti_breakpoints != NULL) {
    _jvmti_breakpoints->oops_do(f);
  }
}

void JvmtiCurrentBreakpoints::gc_epilogue() { 
  if (_jvmti_breakpoints != NULL) {
    _jvmti_breakpoints->gc_epilogue();
  }
}


///////////////////////////////////////////////////////////////
//
// class VM_GetOrSetLocal
//

// Constructor for non-object getter
VM_GetOrSetLocal::VM_GetOrSetLocal(JavaThread* thread, jint depth, int index, BasicType type)
  : _thread(thread)
  , _calling_thread(NULL)
  , _depth(depth)
  , _index(index)
  , _type(type)
  , _set(false)
,_jvf(thread)
  , _result(JVMTI_ERROR_NONE)
{  
}

// Constructor for object or non-object setter
VM_GetOrSetLocal::VM_GetOrSetLocal(JavaThread* thread, jint depth, int index, BasicType type, jvalue value)
  : _thread(thread)
  , _calling_thread(NULL)
  , _depth(depth)
  , _index(index)
  , _type(type)
  , _value(value)
  , _set(true)
,_jvf(thread)
  , _result(JVMTI_ERROR_NONE)
{
}

// Constructor for object getter
VM_GetOrSetLocal::VM_GetOrSetLocal(JavaThread* thread, JavaThread* calling_thread, jint depth, int index)
  : _thread(thread)
  , _calling_thread(calling_thread)
  , _depth(depth)
  , _index(index)
  , _type(T_OBJECT)
  , _set(false)
,_jvf(thread)
  , _result(JVMTI_ERROR_NONE)
{
}


vframe VM_GetOrSetLocal::get_vframe() {
  vframe vf = _thread->last_java_vframe();
  int d = 0;
  while (!vf.done() && (d < _depth)) {
vf.next();
    d++;
  }
  return vf;
}

// Check that the klass is assignable to a type with the given signature.
// Another solution could be to use the function Klass::is_subtype_of(type).
// But the type class can be forced to load/initialize eagerly in such a case.
// This may cause unexpected consequences like CFLH or class-init JVMTI events.
// It is better to avoid such a behavior.
bool VM_GetOrSetLocal::is_assignable(const char* ty_sign, Klass* klass, Thread* thread) {
  assert(ty_sign != NULL, "type signature must not be NULL");
  assert(thread != NULL, "thread must not be NULL");
  assert(klass != NULL, "klass must not be NULL");

  int len = (int) strlen(ty_sign);
  if (ty_sign[0] == 'L' && ty_sign[len-1] == ';') { // Need pure class/interface name
    ty_sign++;
    len -= 2;
  }
  symbolHandle ty_sym = oopFactory::new_symbol_handle(ty_sign, len, thread);
  if (klass->name() == ty_sym()) {
    return true;
  }
  // Compare primary supers
  int super_depth = klass->super_depth();
  int idx;
  for (idx = 0; idx < super_depth; idx++) {
if(Klass::cast(KlassTable::getKlassByKlassId(klass->primary_super_of_depth(idx)).as_klassOop())->name()==ty_sym()){
      return true;
    }
  }
  // Compare secondary supers
  objArrayOop sec_supers = klass->secondary_supers(); 
  for (idx = 0; idx < sec_supers->length(); idx++) {
    if (Klass::cast((klassOop) sec_supers->obj_at(idx))->name() == ty_sym()) {
      return true;
    }
  }
  return false;
}

// Checks error conditions:
//   JVMTI_ERROR_INVALID_SLOT
//   JVMTI_ERROR_TYPE_MISMATCH
// Returns: 'true' - everything is Ok, 'false' - error code
  
bool VM_GetOrSetLocal::check_slot_type(const vframe& jvf) {
methodOop method_oop=jvf.method();
  if (!method_oop->has_localvariable_table()) {
    // Just to check index boundaries
    jint extra_slot = (_type == T_LONG || _type == T_DOUBLE) ? 1 : 0;
    if (_index < 0 || _index + extra_slot >= method_oop->max_locals()) {
      _result = JVMTI_ERROR_INVALID_SLOT;
      return false;
    }
    return true;
  }

  jint num_entries = method_oop->localvariable_table_length();
  if (num_entries == 0) {
    _result = JVMTI_ERROR_INVALID_SLOT;
    return false;	// There are no slots
  }
  int signature_idx = -1;
int vf_bci=jvf.bci();
  LocalVariableTableElement* table = method_oop->localvariable_table_start();
  for (int i = 0; i < num_entries; i++) {
    int start_bci = table[i].start_bci;
    int end_bci = start_bci + table[i].length;

    // Here we assume that locations of LVT entries
    // with the same slot number cannot be overlapped
    if (_index == (jint) table[i].slot && start_bci <= vf_bci && vf_bci <= end_bci) {
      signature_idx = (int) table[i].descriptor_cp_index;
      break;
    }
  }
  if (signature_idx == -1) {
    _result = JVMTI_ERROR_INVALID_SLOT;
    return false;	// Incorrect slot index
  }
  symbolOop   sign_sym  = method_oop->constants()->symbol_at(signature_idx);
  const char* signature = (const char *) sign_sym->as_utf8();
  BasicType slot_type = char2type(signature[0]);

  switch (slot_type) {
  case T_BYTE:
  case T_SHORT:
  case T_CHAR:
  case T_BOOLEAN:
    slot_type = T_INT;
    break;
  case T_ARRAY:
    slot_type = T_OBJECT;
    break;
  };    
  if (_type != slot_type) {
    _result = JVMTI_ERROR_TYPE_MISMATCH;
    return false;
  }

  jobject jobj = _value.l;
  if (_set && slot_type == T_OBJECT && jobj != NULL) { // NULL reference is allowed
    // Check that the jobject class matches the return type signature.
    // Azul implementation can use Thread::current();
    JavaThread* cur_thread = JavaThread::current();
    HandleMark hm(cur_thread);

Handle obj=Handle(cur_thread,JNIHandles::resolve_as_ref_external_guard(jobj));
    NULL_CHECK(obj, (_result = JVMTI_ERROR_INVALID_OBJECT, false));
    KlassHandle ob_kh = KlassHandle(cur_thread, obj->klass());
    NULL_CHECK(ob_kh, (_result = JVMTI_ERROR_INVALID_OBJECT, false));

    if (!is_assignable(signature, Klass::cast(ob_kh()), cur_thread)) {
      _result = JVMTI_ERROR_TYPE_MISMATCH;
      return false;
    }
  }
  return true;
}


bool VM_GetOrSetLocal::doit_prologue() { 
if(_thread->root_Java_frame()){
    _result = JVMTI_ERROR_NO_MORE_FRAMES;
    return false;
  }
 
  return true;
}

void VM_GetOrSetLocal::doit() {
  // Must acquire both the Compile_lock (for deoptimization) and the
  // Threads_lock (to scan all threads).  To avoid deadlock we grab the
  // Compile_lock first.
MutexLocker ml(Compile_lock,Thread::current());
  EnforceSafepoint es; 

_jvf=get_vframe();

  if (_jvf.done()) {
    _result = JVMTI_ERROR_NO_MORE_FRAMES;
    return;
  }

  if (!_jvf.get_frame().is_java_frame()) {
    _result = JVMTI_ERROR_OPAQUE_FRAME;
    return;
  }

  if (_jvf.method()->is_native()) {
    _result = JVMTI_ERROR_OPAQUE_FRAME;
    return;
  }

  if (!check_slot_type(_jvf)) {
    return;
  }
  
  if (_set) {
    Unimplemented();
    //// Force deoptimization of frame if compiled because it's
    //// possible the compiler emitted some locals as constant values,
    //// meaning they are not mutable.
    //Thread* current_thread = Thread::current();
    //frame fr = _jvf.get_frame();
    //if (fr.is_java_frame() && fr.is_compiled_frame()) {
    //  {
    //    MutexLocker ml2(NMethodBucket_lock);
    //    _jvf.nm()->deoptimize();
    //
    //    VM_Deoptimize op;
    //    VMThread::execute(&op);
    //  }
    //
    //  // Now store a new value for the local which will be applied
    //  // once deoptimization occurs. Note however that while this
    //  // write is deferred until deoptimization actually happens
    //  // can vframe created after this point will have its locals
    //  // reflecting this update so as far as anyone can see the
    //  // write has already taken place.
    //
    //  // If we are updating an oop then get the oop from the handle
    //  // since the handle will be long gone by the time the deopt
    //  // happens. The oop stored in the deferred local will be
    //  // gc'd on its own.
    //  if (_type == T_OBJECT) {
    //    objectRef ref = JNIHandles::resolve_as_ref_external_guard(_value.l);
    //    _value.l = *((jobject*)&ref);
    //  }
    //
    //  _jvf.update_local(_thread, _type, _index, _value);
    //
    //  return;
    //}

    _jvf.set_java_local(_index, _value, _type);

  } else {
    *(intptr_t*)&_value = _jvf.java_local(_thread, _index);
    if (_type == T_OBJECT) {
      // Wrap the oop to be returned in a local JNI handle since
      // oops_do() no longer applies after doit() is finished.
      _value.l = JNIHandles::make_local(_calling_thread, (*(objectRef*)&(_value.l)).as_oop());
    }
  }
}


bool VM_GetOrSetLocal::allow_nested_vm_operations() const {
  return true; // May need to deoptimize
}


/////////////////////////////////////////////////////////////////////////////////////////

//
// class JvmtiSuspendControl - see comments in jvmtiImpl.hpp
//

jvmtiError JvmtiSuspendControl::suspend(JavaThread *java_thread) {  
  // external suspend should have caught suspending a thread twice

  // Immediate suspension required for JPDA back-end so JVMTI agent threads do
  // not deadlock due to later suspension on transitions while holding
  // raw monitors.  Passing true causes the immediate suspension.
  // java_suspend() will catch threads in the process of exiting
  // and will ignore them.
if(!java_thread->set_jvmti_suspend_request()){
    return JVMTI_ERROR_THREAD_SUSPENDED;
  }

  // It would be nice to have the following assertion in all the time,
  // but it is possible for a racing resume request to have resumed
  // this thread right after we suspended it. Temporarily enable this
  // assertion if you are chasing a different kind of bug.
  //
  // assert(java_lang_Thread::thread(java_thread->threadObj()) == NULL ||
  //   java_thread->is_being_ext_suspended(), "thread is not suspended");

  if (java_lang_Thread::thread(java_thread->threadObj()) == NULL) {
    // check again because we can get delayed in java_suspend():
    // the thread is in process of exiting.
    return JVMTI_ERROR_INVALID_THREAD;
  }

  return JVMTI_ERROR_NONE;
}

bool JvmtiSuspendControl::resume(JavaThread *java_thread) {  
  // external suspend should have caught resuming a thread twice
  assert(java_thread->is_being_ext_suspended(), "thread should be suspended");

  // resume thread
  {
    // must always grab Threads_lock, see JVM_SuspendThread
MutexLockerAllowGC ml(Threads_lock,JavaThread::current());
java_thread->java_resume(JavaThread::jvmti_suspend);
  }
 
  return true;
}


void JvmtiSuspendControl::print() {
#ifndef PRODUCT
  MutexLockerAllowGC mu(Threads_lock, JavaThread::current());
  ResourceMark rm;

  tty->print("Suspended Threads: [");
  for (JavaThread *thread = Threads::first(); thread != NULL; thread = thread->next()) {
#if JVMTI_TRACE
    const char *name   = JvmtiTrace::safe_get_thread_name(thread);
#else
    const char *name   = "";
#endif /*JVMTI_TRACE */
    tty->print("%s(%c ", name, thread->is_being_ext_suspended() ? 'S' : '_');
if(thread->root_Java_frame()){
      tty->print("no stack");
    }
    tty->print(") ");
  }
  tty->print_cr("]");
#endif  
}
