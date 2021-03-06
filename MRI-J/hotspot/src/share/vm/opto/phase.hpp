/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef PHASE_HPP
#define PHASE_HPP

#include "allocation.hpp"
#include "timer.hpp"
class Compile;

//------------------------------Phase------------------------------------------
// Most optimizations are done in Phases.  Creating a phase does any long
// running analysis required, and caches the analysis in internal data
// structures.  Later the analysis is queried using transform() calls to 
// guide transforming the program.  When the Phase is deleted, so is any
// cached analysis info.  This basic Phase class mostly contains timing and
// memory management code.
class Phase : public StackObj {
public:
  enum PhaseNumber {
    Compiler,                   // Top-level compiler phase
    Parser,                     // Parse bytecodes
    Remove_Useless,             // Remove useless nodes
    Optimistic,                 // Optimistic analysis phase
    GVN,                        // Pessimistic global value numbering phase
    Ins_Select,                 // Instruction selection phase
    Copy_Elimination,           // Copy Elimination
    Dead_Code_Elimination,      // DCE and compress Nodes
    Conditional_Constant,       // Conditional Constant Propagation
    CFG,                        // Build a CFG
    DefUse,                     // Build Def->Use chains
    Register_Allocation,        // Register allocation, duh
    LIVE,                       // Dragon-book LIVE range problem
    Interference_Graph,         // Building the IFG
    Coalesce,                   // Coalescing copies
    Conditional_CProp,          // Conditional Constant Propagation
    Ideal_Loop,                 // Find idealized trip-counted loops
    Macro_Expand,               // Expand macro nodes
    Peephole,                   // Apply peephole optimizations
    last_phase
  };
protected:
  enum PhaseNumber _pnum;       // Phase number (for stat gathering)

#ifndef PRODUCT
  static int _total_bytes_compiled;

  // accumulated timers
  static elapsedTimer _t_totalCompilation;
  static elapsedTimer _t_methodCompilation;
#endif

// The next timers used for LogCompilation
  static elapsedTimer _t_parser;
  static elapsedTimer _t_optimizer;
  static elapsedTimer   _t_idealLoop;
  static elapsedTimer   _t_ccp;
  static elapsedTimer _t_matcher;
  static elapsedTimer _t_registerAllocation;
  static elapsedTimer _t_output;

#ifndef PRODUCT
  static elapsedTimer _t_graphReshaping;
  static elapsedTimer _t_scheduler;
  static elapsedTimer _t_removeEmptyBlocks;
  static elapsedTimer _t_macroExpand;
  static elapsedTimer _t_peephole;
  static elapsedTimer _t_codeGeneration;
  static elapsedTimer _t_registerMethod;
  static elapsedTimer _t_temporaryTimer1;
  static elapsedTimer _t_temporaryTimer2;

// Subtimers for _t_optimizer 
  static elapsedTimer   _t_iterGVN;
  static elapsedTimer   _t_iterGVN2;

// Subtimers for _t_registerAllocation 
  static elapsedTimer   _t_ctorChaitin;
  static elapsedTimer   _t_buildIFGphysical;
  static elapsedTimer   _t_computeLive;
  static elapsedTimer   _t_regAllocSplit;
  static elapsedTimer   _t_postAllocCopyRemoval;
  static elapsedTimer   _t_fixupSpills;

// Subtimers for _t_output 
  static elapsedTimer   _t_instrSched;
  static elapsedTimer   _t_buildOopMaps;
#endif
public:
  Compile * C;
  Phase( PhaseNumber pnum );
#ifndef PRODUCT
  static void print_timers();
#endif
};

#endif // PHASE_HPP
