"""Test that anonymous structs/unions are transparent to member access"""

import os, time
import unittest2
import lldb
from lldbtest import *
import lldbutil

class AnonymousTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @dsym_test
    def test_expr_nest_with_dsym(self):
        self.buildDsym()
        self.expr_nest()

    @dsym_test
    def test_expr_child_with_dsym(self):
        self.buildDsym()
        self.expr_child()

    @dsym_test
    def test_expr_grandchild_with_dsym(self):
        self.buildDsym()
        self.expr_grandchild()

    @dsym_test
    def test_expr_parent(self):
        self.buildDsym()
        self.expr_parent()

    @dsym_test
    def test_expr_null(self):
        self.buildDsym()
        self.expr_null()

    @dsym_test
    def test_child_by_name(self):
        self.buildDsym()
        self.child_by_name()

    @skipIfGcc # llvm.org/pr15036: LLDB generates an incorrect AST layout for an anonymous struct when DWARF is generated by GCC
    @skipIfIcc # llvm.org/pr15036: LLDB generates an incorrect AST layout for an anonymous struct when DWARF is generated by ICC
    @dwarf_test
    def test_expr_nest_with_dwarf(self):
        self.buildDwarf()
        self.expr_nest()

    @dwarf_test
    def test_expr_child_with_dwarf(self):
        self.skipTest("Skipped because LLDB asserts due to an incorrect AST layout for an anonymous struct: see llvm.org/pr15036")
        self.buildDwarf()
        self.expr_child()

    @skipIfGcc # llvm.org/pr15036: This particular regression was introduced by r181498
    @skipIfIcc # llvm.org/pr15036: This particular regression was introduced by r181498
    @dwarf_test
    def test_expr_grandchild_with_dwarf(self):
        self.buildDwarf()
        self.expr_grandchild()

    @dwarf_test
    def test_expr_parent(self):
        self.buildDwarf()
        self.expr_parent()

    @expectedFailureFreeBSD('llvm.org/pr21550')
    @dwarf_test
    def test_expr_null(self):
        self.buildDwarf()
        self.expr_null()

    @dwarf_test
    def test_child_by_name_with_dwarf(self):
        self.buildDwarf()
        self.child_by_name()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line numbers to break in main.c.
        self.line0 = line_number('main.c', '// Set breakpoint 0 here.')
        self.line1 = line_number('main.c', '// Set breakpoint 1 here.')
        self.line2 = line_number('main.c', '// Set breakpoint 2 here.')

    def common_setup(self, line):
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Set breakpoints inside and outside methods that take pointers to the containing struct.
        lldbutil.run_break_set_by_file_and_line (self, "main.c", line, num_expected_locations=1, loc_exact=True)

        self.runCmd("run", RUN_SUCCEEDED)

        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['stopped',
                       'stop reason = breakpoint'])

        # The breakpoint should have a hit count of 1.
        self.expect("breakpoint list -f", BREAKPOINT_HIT_ONCE,
            substrs = [' resolved, hit count = 1'])

    def expr_nest(self):
        self.common_setup(self.line0)

        # These should display correctly.
        self.expect("expression n->foo.d", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["= 4"])
            
        self.expect("expression n->b", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["= 2"])

    def expr_child(self):
        self.common_setup(self.line1)

        # These should display correctly.
        self.expect("expression c->foo.d", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["= 4"])
            
        self.expect("expression c->grandchild.b", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["= 2"])

    def expr_grandchild(self):
        self.common_setup(self.line2)

        # These should display correctly.
        self.expect("expression g.child.foo.d", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["= 4"])
            
        self.expect("expression g.child.b", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["= 2"])

    def expr_parent(self):
        if "clang" in self.getCompiler() and "3.4" in self.getCompilerVersion():
            self.skipTest("llvm.org/pr16214 -- clang emits partial DWARF for structures referenced via typedef")
        self.common_setup(self.line2)

        # These should display correctly.
        self.expect("expression pz", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["(type_z *) $", " = 0x0000"])

        self.expect("expression z.y", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ["(type_y) $", "dummy = 2"])

    def expr_null(self):
        self.common_setup(self.line2)

        # This should fail because pz is 0, but it succeeds on OS/X.
        # This fails on Linux with an upstream error "Couldn't dematerialize struct", as does "p *n" with "int *n = 0".
        # Note that this can also trigger llvm.org/pr15036 when run interactively at the lldb command prompt.
        self.expect("expression *(type_z *)pz", error = True)

    def child_by_name(self):
        exe = os.path.join (os.getcwd(), "a.out")
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)

        break_in_main = target.BreakpointCreateBySourceRegex ('// Set breakpoint 2 here.', lldb.SBFileSpec("main.c"))
        self.assertTrue(break_in_main, VALID_BREAKPOINT)

        process = target.LaunchSimple (None, None, self.get_process_working_directory())
        self.assertTrue (process, PROCESS_IS_VALID)

        threads = lldbutil.get_threads_stopped_at_breakpoint (process, break_in_main)
        if len(threads) != 1:
            self.fail ("Failed to stop at breakpoint in main.")

        thread = threads[0]
        frame = thread.frames[0]

        if not frame.IsValid():
            self.fail ("Failed to get frame 0.")

        var_n = frame.FindVariable("n")
        if not var_n.IsValid():
            self.fail ("Failed to get the variable 'n'")

        elem_a = var_n.GetChildMemberWithName("a")
        if not elem_a.IsValid():
            self.fail ("Failed to get the element a in n")

        error = lldb.SBError()
        value = elem_a.GetValueAsSigned(error, 1000)
        if not error.Success() or value != 0:
            self.fail ("failed to get the correct value for element a in n")

if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
