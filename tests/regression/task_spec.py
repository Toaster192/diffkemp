"""Functions for working with modules used by regression tests."""

import os
import shutil

from diffkemp.config import Config
from diffkemp.llvm_ir.kernel_llvm_source_builder import KernelLlvmSourceBuilder
from diffkemp.llvm_ir.kernel_source_tree import KernelSourceTree
from diffkemp.semdiff.pattern_config import PatternConfig
from diffkemp.semdiff.result import Result
from diffkemp.snapshot import Snapshot
from diffkemp.utils import get_llvm_version


base_path = os.path.abspath(".")
patterns_path = os.path.abspath("tests/regression/patterns")
specs_path = os.path.abspath("tests/regression/test_specs")
tasks_path = os.path.abspath("tests/regression/kernel_modules")


class FunctionSpec:
    """"
    Specification of a function in kernel along the modules/source files where
    it is located and the expected result of the function comparison between
    two kernel versions.
    """
    def __init__(self, name, result, old_module=None, new_module=None):
        self.name = name
        self.old_module = old_module
        self.new_module = new_module
        if result == "generic":
            self.result = Result.Kind.NONE
        else:
            self.result = Result.Kind[result.upper()]


class TaskSpec:
    """
    Task specification representing testing scenario.
    Contains a list of functions to be compared with DiffKemp during the test.
    """
    def __init__(self, spec, task_name, kernel_path):
        self.old_kernel_dir = os.path.join(kernel_path, spec["old_kernel"])
        self.new_kernel_dir = os.path.join(kernel_path, spec["new_kernel"])
        self.name = task_name
        self.task_dir = os.path.join(tasks_path, task_name)
        if "pattern_config" in spec:
            if get_llvm_version() >= 15:
                config_filename = spec["pattern_config"]["opaque"]
            else:
                config_filename = spec["pattern_config"]["explicit"]
            self.pattern_config = PatternConfig.create_from_file(
                path=os.path.join(patterns_path, config_filename),
                patterns_path=base_path
            )
        else:
            self.pattern_config = None
        if "control_flow_only" in spec:
            self.control_flow_only = spec["control_flow_only"]
        else:
            self.control_flow_only = False

        # Create LLVM sources and configuration
        self.old_kernel = KernelSourceTree(
            self.old_kernel_dir, KernelLlvmSourceBuilder(self.old_kernel_dir))
        self.new_kernel = KernelSourceTree(
            self.new_kernel_dir, KernelLlvmSourceBuilder(self.new_kernel_dir))
        self.old_snapshot = Snapshot(self.old_kernel, self.old_kernel)
        self.new_snapshot = Snapshot(self.new_kernel, self.new_kernel)
        self.config = Config(self.old_snapshot, self.new_snapshot, False,
                             False, self.pattern_config,
                             self.control_flow_only, False, False, False, None)

        self.functions = dict()

    def finalize(self):
        """
        Task finalization - should be called before the task is destroyed.
        """
        self.old_kernel.finalize()
        self.new_kernel.finalize()

    def add_function_spec(self, fun, result):
        """Add a function comparison specification."""
        self.functions[fun] = FunctionSpec(fun, result)

    def build_modules_for_function(self, fun):
        """
        Build LLVM modules containing definition of the compared function in
        both kernels.
        """
        # Since PyTest may share KernelSourceTree objects among tasks, we need
        # to explicitly initialize kernels.
        self.old_kernel.initialize()
        self.new_kernel.initialize()
        mod_old = self.old_kernel.get_module_for_symbol(fun)
        mod_new = self.new_kernel.get_module_for_symbol(fun)
        self.functions[fun].old_module = mod_old
        self.functions[fun].new_module = mod_new
        return mod_old, mod_new

    def _file_name(self, suffix, ext, name=None):
        """
        Get name of a task file having the given name, suffix, and extension.
        """
        return os.path.join(self.task_dir,
                            "{}_{}.{}".format(name or self.name, suffix, ext))

    def old_llvm_file(self, name=None):
        """Name of the old LLVM file in the task dir."""
        return self._file_name("old", "ll", name)

    def new_llvm_file(self, name=None):
        """Name of the new LLVM file in the task dir."""
        return self._file_name("new", "ll", name)

    def old_src_file(self, name=None):
        """Name of the old C file in the task dir."""
        return self._file_name("old", "c", name)

    def new_src_file(self, name=None):
        """Name of the new C file in the task dir."""
        return self._file_name("new", "c", name)

    def prepare_dir(self, old_module, new_module, old_src, new_src, name=None):
        """
        Create the task directory and copy the LLVM and the C files there.
        :param old_module: Old LLVM module (instance of LlvmModule).
        :param old_src: C source from the old kernel version to be copied.
        :param new_module: New LLVM module (instance of LlvmModule).
        :param new_src: C source from the new kernel version to be copied.
        :param name: Optional parameter to specify the new file names. If None
                     then the spec name is used.
        """
        if not os.path.isdir(self.task_dir):
            os.mkdir(self.task_dir)

        if not os.path.isfile(self.old_llvm_file(name)):
            shutil.copyfile(old_module.llvm, self.old_llvm_file(name))
        if old_src and not os.path.isfile(self.old_src_file(name)):
            shutil.copyfile(old_src, self.old_src_file(name))
        if not os.path.isfile(self.new_llvm_file(name)):
            shutil.copyfile(new_module.llvm, self.new_llvm_file(name))
        if new_src and not os.path.isfile(self.new_src_file(name)):
            shutil.copyfile(new_src, self.new_src_file(name))


class SysctlTaskSpec(TaskSpec):
    """
    Task specification for test of sysctl comparison.
    Extends TaskSpec by data variable and proc handler function.
    """
    def __init__(self, spec, task_name, kernel_path, data_var):
        TaskSpec.__init__(self, spec, task_name, kernel_path)
        self.data_var = data_var
        self.proc_handler = None
        self.old_sysctl_module = None
        self.new_sysctl_module = None

    def add_proc_handler(self, proc_handler, result):
        """Add proc handler function to the spec."""
        self.proc_handler = proc_handler
        self.add_function_spec(proc_handler, result)

    def get_proc_handler_spec(self):
        """Retrieve specification of the proc handler function."""
        return self.functions[self.proc_handler]

    def add_data_var_function(self, fun, result):
        """Add a new function using the data variable."""
        self.add_function_spec(fun, result)

    def build_sysctl_module(self):
        """Build the compared sysctl modules into LLVM."""
        self.old_sysctl_module = self.old_kernel.get_sysctl_module(self.name)
        self.new_sysctl_module = self.new_kernel.get_sysctl_module(self.name)


class ModuleParamSpec(TaskSpec):
    """
    Task specification for test of kernel module parameter comparison.
    Extends TaskSpec by module and parameter specification.
    """
    def __init__(self, spec, dir, mod, param, kernel_path):
        TaskSpec.__init__(self, spec, "{}-{}".format(mod, param), kernel_path)
        self.dir = dir
        self.mod = mod
        self.param = param
        self.old_module = None
        self.new_module = None

    def build_module(self):
        """Build the compared kernel modules into LLVM."""
        self.old_module = self.old_kernel.get_kernel_module(self.dir, self.mod)
        self.new_module = self.new_kernel.get_kernel_module(self.dir, self.mod)

    def get_param(self):
        """Get the name of the global variable representing the parameter."""
        return self.old_module.find_param_var(self.param)


class DiffSpec:
    """
    Specification of a syntax difference. Contains the name of the differing
    symbol and its old and new definition.
    """
    def __init__(self, symbol, def_old, def_new):
        self.symbol = symbol
        self.def_old = def_old
        self.def_new = def_new


class SyntaxDiffSpec(TaskSpec):
    """
    Task specification for test of syntax difference.
    Extends TaskSpec by concrete syntax differences that should be found by
    DiffKemp. These are currently intended to be macros or inline assemblies.
    """
    def __init__(self, spec, task_name, kernel_path):
        TaskSpec.__init__(self, spec, task_name, kernel_path)
        self.equal_symbols = set()
        self.syntax_diffs = dict()

    def add_equal_symbol(self, symbol):
        """Add a symbol that should not be present in the result"""
        self.equal_symbols.add(symbol)

    def add_syntax_diff_spec(self, symbol, def_old, def_new):
        """Add an expected syntax difference"""
        self.syntax_diffs[symbol] = DiffSpec(symbol, def_old, def_new)
