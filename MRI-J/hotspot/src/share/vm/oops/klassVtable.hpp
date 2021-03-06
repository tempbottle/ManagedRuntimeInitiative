/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef KLASSVTABLE_HPP
#define KLASSVTABLE_HPP

#include "allocation.hpp"
#include "handles.hpp"
#include "growableArray.hpp"

// A klassVtable abstracts the variable-length vtable that is embedded in instanceKlass
// and arrayKlass.  klassVtable objects are used just as convenient transient accessors to the vtable,
// not to actually hold the vtable data.
// Note: the klassVtable should not be accessed before the class has been verified
// (until that point, the vtable is uninitialized).

// Currently a klassVtable contains a direct reference to the vtable data, and is therefore
// not preserved across GCs.

class vtableEntry;

class klassVtable : public ResourceObj {
  KlassHandle  _klass;            // my klass 
  int          _tableOffset;      // offset of start of vtable data within klass
  int          _length;           // length of vtable (number of entries)
#ifndef PRODUCT
  int          _verify_count;     // to make verify faster
#endif

  // Ordering important, so greater_than (>) can be used as an merge operator.
  enum AccessType {
    acc_private         = 0,
    acc_package_private = 1,
    acc_publicprotected = 2
  };

 public:
  klassVtable(KlassHandle h_klass, void* base, int length) : _klass(h_klass) {
    _tableOffset = (address)base - (address)h_klass(); _length = length;
  }

  // accessors
  vtableEntry* table() const      { return (vtableEntry*)(address(_klass()) + _tableOffset); }
  KlassHandle klass() const       { return _klass;  }
  int length() const              { return _length; }
  inline methodOop method_at(int i) const;
  inline methodOop unchecked_method_at(int i) const;
  inline objectRef*      adr_method_at(int i) const;

  // searching; all methods return -1 if not found
  int index_of(methodOop m) const                         { return index_of(m, _length); }
  int index_of_miranda(symbolOop name, symbolOop signature);

  void initialize_vtable(bool checkconstraints, TRAPS);   // initialize vtable of a new klass
  
  // conputes vtable length (in words) and the number of miranda methods
  static void compute_vtable_size_and_num_mirandas(int &vtable_length, int &num_miranda_methods,
						   klassOop super, objArrayOop methods, 
						   AccessFlags class_flags, oop classloader,
						   symbolOop classname, objArrayOop local_interfaces);

  // RedefineClasses() API support:
  // If any entry of this vtable points to any of old_methods,
  // replace it with the corresponding new_method.
  // trace_name_printed is set to true if the current call has
  // printed the klass name so that other routines in the adjust_*
  // group don't print the klass name.
  void adjust_method_entries(methodRef* old_methods, methodRef* new_methods,
                             int methods_length, bool * trace_name_printed);

  // Garbage collection
  void oop_follow_contents();
  void oop_adjust_pointers();

  // Parallel Old
  void oop_follow_contents(ParCompactionManager* cm);
  void oop_update_pointers(ParCompactionManager* cm);
  void oop_update_pointers(ParCompactionManager* cm,
			   HeapWord* beg_addr, HeapWord* end_addr);

  // GenPauselessGC
  void GPGC_oop_follow_contents(GPGC_GCManagerNewStrong* gcm);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldStrong* gcm);
  void GPGC_oop_follow_contents(GPGC_GCManagerNewFinal* gcm);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldFinal* gcm);
  void GPGC_verify_no_cardmark();

  // Iterators
  void oop_oop_iterate(OopClosure* blk);
  void oop_oop_iterate_m(OopClosure* blk, MemRegion mr);

  // Debugging code
  void print()                                              PRODUCT_RETURN;
  void verify(outputStream* st, bool force = false);
  static void print_statistics()                            PRODUCT_RETURN;

#ifndef PRODUCT
  bool check_no_old_entries();
  void dump_vtable();
#endif

 protected:
  friend class vtableEntry;
 private:
  void copy_vtable_to(vtableEntry* start);
  int  initialize_from_super(KlassHandle super);
  int  index_of(methodOop m, int len) const; // same as index_of, but search only up to len
  void put_method_at(methodOop m, int index);
  static bool needs_new_vtable_entry(methodOop m, klassOop super, oop classloader, symbolOop classname, AccessFlags access_flags);
  AccessType vtable_accessibility_at(int i);

  bool update_super_vtable(instanceKlass* klass, methodHandle target_method, int super_vtable_len, bool checkconstraints, TRAPS);

  // support for miranda methods
  bool is_miranda_entry_at(int i);
  void fill_in_mirandas(int& initialized);
  static bool is_miranda(methodOop m, objArrayOop class_methods, klassOop super);
  static void add_new_mirandas_to_list(GrowableArray<methodOop>* list_of_current_mirandas, objArrayOop current_interface_methods, objArrayOop class_methods, klassOop super);
  static void get_mirandas(GrowableArray<methodOop>* mirandas, klassOop super, objArrayOop class_methods, objArrayOop local_interfaces);
  static int get_num_mirandas(klassOop super, objArrayOop class_methods, objArrayOop local_interfaces);
    
  
  void verify_against(outputStream* st, klassVtable* vt, int index);
  inline instanceKlass* ik() const;    
};


// private helper class for klassVtable
// description of entry points:
//    destination is interpreted:
//      from_compiled_code_entry_point -> c2iadapter
//      from_interpreter_entry_point   -> interpreter entry point
//    destination is compiled:
//      from_compiled_code_entry_point -> codeblob entry point
//      from_interpreter_entry_point   -> i2cadapter
class vtableEntry VALUE_OBJ_CLASS_SPEC {
 public:
  // size in words
  static int size() {
    return sizeof(vtableEntry) / sizeof(HeapWord);
  }
  static int method_offset_in_bytes() { return offset_of(vtableEntry, _method); }
methodRef method_ref()const{return lvb_methodRef(&_method);}
  methodOop  method() const           { return lvb_methodRef(&_method).as_methodOop(); }
  methodRef* method_addr()            { return &_method; }

 private:
  methodRef _method;
void set_ref(methodRef method){POISON_AND_STORE_REF(&_method,method);}
  void set(methodOop method)  { assert(method != NULL, "use clear"); POISON_AND_STORE_REF(&_method, methodRef(method)); }
void clear(){_method=nullRef;}
  void print()                                        PRODUCT_RETURN;
  void verify(klassVtable* vt, outputStream* st);

  friend class klassVtable;
};


inline methodOop klassVtable::method_at(int i) const { 
  assert(i >= 0 && i < _length, "index out of bounds");
  assert(table()[i].method() != NULL, "should not be null");
  assert(oop(table()[i].method())->is_method(), "should be method");
  return table()[i].method();
}

inline methodOop klassVtable::unchecked_method_at(int i) const { 
  assert(i >= 0 && i < _length, "index out of bounds");
  return table()[i].method();
}

inline objectRef* klassVtable::adr_method_at(int i) const { 
  // Allow one past the last entry to be referenced; useful for loop bounds.
  assert(i >= 0 && i <= _length, "index out of bounds");
  return (objectRef*)(address(table() + i) + vtableEntry::method_offset_in_bytes());
}

// --------------------------------------------------------------------------------
class klassItable;
class itableMethodEntry;

class itableOffsetEntry VALUE_OBJ_CLASS_SPEC {
 private:
  klassRef _interface;
  int      _offset;
 public:
klassRef*interface_addr(){return&_interface;}
  klassOop  interface_klass() const         { return lvb_klassRef(&_interface).as_klassOop(); }
  bool      interface_klass_is_null() const { return (_interface == nullRef); }
  int       offset() const                  { return _offset; }

  static itableMethodEntry* method_entry(klassOop k, int offset) { return (itableMethodEntry*)(((address)k) + offset); }
  itableMethodEntry* first_method_entry(klassOop k)              { return method_entry(k, _offset); }

  void initialize(klassOop interf, int offset) { _interface = POISON_KLASSREF(klassRef(interf)); _offset = offset; }

  // Static size and offset accessors
  static int size()                       { return sizeof(itableOffsetEntry) / HeapWordSize; }    // size in words
  static int interface_offset_in_bytes()  { return offset_of(itableOffsetEntry, _interface); }
  static int offset_offset_in_bytes()     { return offset_of(itableOffsetEntry, _offset); }

  friend class klassItable;
};


class itableMethodEntry VALUE_OBJ_CLASS_SPEC { 
 private:
  methodRef _method;

 public:
methodRef*method_addr(){return&_method;}
  methodOop  method() const      { return lvb_methodRef(&_method).as_methodOop(); }

void clear(){_method=nullRef;}

  void initialize(methodOop method); 

  // Static size and offset accessors
  static int size()                         { return sizeof(itableMethodEntry) / HeapWordSize; }  // size in words
  static int method_offset_in_bytes()       { return offset_of(itableMethodEntry, _method); }

  friend class klassItable;
};

//
// Format of an itable
//
//    ---- offset table ---
//    klassOop of interface 1             \_
//    offset to vtable from start of oop  / offset table entry
//    ...
//    klassOop of interface n             \_
//    offset to vtable from start of oop  / offset table entry
//    --- vtable for interface 1 ---
//    methodOop                           \_
//    compiler entry point                / method table entry
//    ...
//    methodOop                           \_
//    compiler entry point                / method table entry
//    -- vtable for interface 2 ---
//    ...
//      
class klassItable : public ResourceObj {
 private:
  instanceKlassHandle  _klass;             // my klass 
  int                  _table_offset;      // offset of start of itable data within klass (in words)
  int                  _size_offset_table; // size of offset table (in itableOffset entries)
  int                  _size_method_table; // size of methodtable (in itableMethodEntry entries)

  void initialize_itable_for_interface(int method_table_offset, KlassHandle interf_h, bool checkconstraints, TRAPS);
 public:
  klassItable(instanceKlassHandle klass);

  itableOffsetEntry* offset_entry(int i) { assert(0 <= i && i <= _size_offset_table, "index out of bounds");
                                           return &((itableOffsetEntry*)vtable_start())[i]; }

  itableMethodEntry* method_entry(int i) { assert(0 <= i && i <= _size_method_table, "index out of bounds");
                                           return &((itableMethodEntry*)method_start())[i]; }
  
  int size_offset_table()                { return _size_offset_table; }

  // Initialization
  void initialize_itable(bool checkconstraints, TRAPS);    

  // Updates
  void initialize_with_method(methodOop m);

  // RedefineClasses() API support:
  // if any entry of this itable points to any of old_methods,
  // replace it with the corresponding new_method.
  // trace_name_printed is set to true if the current call has
  // printed the klass name so that other routines in the adjust_*
  // group don't print the klass name.
  void adjust_method_entries(methodRef* old_methods, methodRef* new_methods,
                             int methods_length, bool * trace_name_printed);

  // Garbage collection
  void oop_follow_contents();
  void oop_adjust_pointers();

  // Parallel Old
  void oop_follow_contents(ParCompactionManager* cm);
  void oop_update_pointers(ParCompactionManager* cm);
  void oop_update_pointers(ParCompactionManager* cm,
			   HeapWord* beg_addr, HeapWord* end_addr);

  // GenPauselessGC
  void GPGC_oop_follow_contents(GPGC_GCManagerNewStrong* gcm);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldStrong* gcm);
  void GPGC_oop_follow_contents(GPGC_GCManagerNewFinal* gcm);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldFinal* gcm);
  void GPGC_verify_no_cardmark();

  // Iterators
  void oop_oop_iterate(OopClosure* blk);
  void oop_oop_iterate_m(OopClosure* blk, MemRegion mr);

  // Setup of itable
  static int compute_itable_size(objArrayHandle transitive_interfaces);
  static void setup_itable_offset_table(instanceKlassHandle klass);

  // Resolving of method to index
  static int compute_itable_index(methodOop m);

  // Debugging/Statistics
  static void print_statistics() PRODUCT_RETURN;
 private:  
  intptr_t* vtable_start() const { return ((intptr_t*)_klass()) + _table_offset; }
  intptr_t* method_start() const { return vtable_start() + _size_offset_table * itableOffsetEntry::size(); }

  // Helper methods  
  static int  calc_itable_size(int num_interfaces, int num_methods) { return (num_interfaces * itableOffsetEntry::size()) + (num_methods * itableMethodEntry::size()); }

  // Statistics
  NOT_PRODUCT(static int  _total_classes;)   // Total no. of classes with itables
  NOT_PRODUCT(static long _total_size;)      // Total no. of bytes used for itables

  static void update_stats(int size) PRODUCT_RETURN NOT_PRODUCT({ _total_classes++; _total_size += size; })
};
#endif // KLASSVTABLE_HPP
