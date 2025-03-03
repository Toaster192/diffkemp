"""
Snapshot representation.
Contains necessary information about the snapshot and (mainly) holds
the function list containing mapping of the compared functions to LLVM files
with their definitions.
"""
from diffkemp.llvm_ir.llvm_module import LlvmModule
from diffkemp.llvm_ir.kernel_source_tree import KernelSourceTree
from diffkemp.llvm_ir.source_tree import SourceTree
from diffkemp.llvm_ir.kernel_llvm_source_builder import KernelLlvmSourceBuilder
from diffkemp.utils import get_llvm_version
import datetime
import os
import pkg_resources
import shutil
import sys
import yaml


class Snapshot:
    """
    Project functions snapshot.
    Provides methods for snapshot management. Also includes specification of
    analysed functions. Functions can be split into multiple groups.
    """

    class FunctionDesc:
        """
        Function description.
        Contains LLVM module of the function definition, a global variable,
        and a tag.
        """

        def __init__(self, mod, glob_var, tag):
            self.mod = mod
            self.glob_var = glob_var
            self.tag = tag

    class FunctionGroup:
        """
        Group of functions. Used to group functions using the same global
        variable (LlvmParam).
        """

        def __init__(self):
            self.functions = dict()

    def __init__(self, source_tree=None, snapshot_tree=None,
                 list_kind=None, store_source_dir=True):
        self.source_tree = source_tree
        self.snapshot_tree = snapshot_tree
        self.list_kind = list_kind
        self.fun_groups = dict()
        self.created_time = None
        self.llvm_version = None
        self.store_source_dir = store_source_dir

    @classmethod
    def create_from_source(cls, source_tree, output_dir, list_kind,
                           store_source_dir=True, setup_dir=True):
        """
        Create a snapshot from a project source directory and prepare it for
        snapshot directory generation.
        :param source_tree: Instance of SourceTree representing the project
                            source directory.
        :param output_dir: Snapshot output directory.
        :param list_kind: Snapshot list kind.
        :param store_source_dir: Whether to store source dir path to snapshot.
        :param setup_dir: Whether to recreate the output directory.
        :return: Desired instance of Snapshot.
        """
        output_path = os.path.abspath(output_dir)

        # Cleanup or create the output directory of the new snapshot.
        if setup_dir:
            if os.path.isdir(output_path):
                shutil.rmtree(output_path)
            os.mkdir(output_path)

        snapshot_tree = source_tree.clone_to_dir(output_dir)
        snapshot = cls(source_tree, snapshot_tree, list_kind, store_source_dir)

        return snapshot

    @classmethod
    def load_from_dir(cls, snapshot_dir, config_file="snapshot.yaml"):
        """
        Loads a snapshot from its directory.
        :param snapshot_dir: Target snapshot directory.
        :param config_file: Name of the snapshot configuration file.
        :return: Desired instance of Snapshot.
        """
        snapshot_tree = SourceTree(snapshot_dir)
        loaded_snapshot = cls(None, snapshot_tree)

        with open(os.path.join(snapshot_dir, config_file), "r") as \
                snapshot_yaml:
            loaded_snapshot._from_yaml(snapshot_yaml.read())

        return loaded_snapshot

    def finalize(self):
        """Run finalize for both source trees."""
        if self.source_tree is not None:
            self.source_tree.finalize()
        if self.snapshot_tree is not None:
            self.snapshot_tree.finalize()

    def generate_snapshot_dir(self):
        """Generate the corresponding snapshot directory."""
        # Copy LLVM files and C files to the snapshot.
        self.source_tree.copy_source_files(self.modules(),
                                           self.snapshot_tree)

        # Create the YAML snapshot representation inside the output directory.
        with open(os.path.join(self.snapshot_tree.source_dir,
                               "snapshot.yaml"), "w") as snapshot_yaml:
            snapshot_yaml.write(self.to_yaml())

    def add_fun(self, name, llvm_mod, glob_var=None, tag=None, group=None):
        """
        Add function to the function list.
        :param name: Name of the function.
        :param llvm_mod: LLVM module with the function definition.
        :param glob_var: Global variable whose usage is analysed within the
                         function.
        :param tag: Function tag.
        :param group: Group to put the function to.
        """
        if group not in self.fun_groups:
            self.fun_groups[group] = self.FunctionGroup()
        self.fun_groups[group].functions[name] = self.FunctionDesc(llvm_mod,
                                                                   glob_var,
                                                                   tag)

    def modules(self):
        """
        Get the set of all LLVM modules of all functions in the function list.
        """
        return set([fun.mod for group in self.fun_groups.values()
                    for fun in group.functions.values()
                    if fun.mod is not None])

    def get_by_name(self, name, group=None):
        """
        Get module for the function with the given name in the given group.
        :param name: Name of the function.
        :param group: Group of the function.
        """
        return self.fun_groups[group].functions[name] \
            if group in self.fun_groups and name in self.fun_groups[
            group].functions \
            else None

    def filter(self, functions, group=None):
        """
        Filter only chosen functions in the given group.
        :param functions: List of function names to filter.
        :param group: Group to filter.
        """
        self.fun_groups[group].functions = {f: self.fun_groups[
            group].functions[f] for f in functions}

    def _from_yaml(self, yaml_file):
        """
        Load the snaphot from its YAML representation. Paths are assumed to be
        relative to the root directory.
        :param yaml_file: Contents of the YAML file.
        """
        yaml_loader = (yaml.CSafeLoader if "CSafeLoader" in yaml.__dict__
                       else yaml.SafeLoader)
        yaml_file = yaml.load(yaml_file, Loader=yaml_loader)
        yaml_dict = yaml_file[0]

        self.created_time = yaml_dict["created_time"]
        self.created_time = self.created_time.replace(
            tzinfo=datetime.timezone.utc)

        self.llvm_version = yaml_dict["llvm_version"]

        # Override the default source finder when the snapshot was originally
        # built with a special one. This is useful for snapshots in which
        # it is possible to look for additional symbol defs during comparison.
        # This is now done for snapshots built with KernelLlvmSourceBuilder.
        if yaml_dict["llvm_source_finder"]["kind"] == "kernel_with_builder":
            source_dir = self.snapshot_tree.source_dir
            source_finder = KernelLlvmSourceBuilder(source_dir)
            self.snapshot_tree = KernelSourceTree(source_dir, source_finder)

        # Add source tree if present
        if "source_dir" in yaml_dict and \
                os.path.isdir(yaml_dict["source_dir"]):
            self.source_tree = self.snapshot_tree.clone_to_dir(
                yaml_dict["source_dir"])
        else:
            sys.stderr.write(
                "Warning: snapshot in {} has missing or invalid source_dir, "
                "some comparison features may not be available\n".format(
                    self.snapshot_tree.source_dir))

        self.list_kind = yaml_dict["list_kind"]
        # Load functions (possibly divided into groups)
        if self.list_kind == "function":
            groups = [yaml_dict["list"]]
        else:
            groups = yaml_dict["list"]

        for g in groups:
            if "sysctl" in g:
                group = g["sysctl"]
                functions = g["functions"]
            else:
                group = None
                functions = g
            for f in functions:
                self.add_fun(f["name"],
                             LlvmModule(
                                 os.path.join(os.path.relpath(
                                     self.snapshot_tree.source_dir),
                                     f["llvm"]))
                             if f["llvm"] else None,
                             f["glob_var"],
                             f["tag"],
                             group)

    def to_yaml(self):
        """
        Dump the snapshot as a YAML string. Paths to files are given relative
        to the source snapshot directory.
        :return: YAML string.
        """
        # Create the function group list.
        fun_yaml_dict = [{
            self.list_kind: group_name,
            "functions": [{
                "name": fun_name,
                "llvm": os.path.relpath(fun_desc.mod.llvm,
                                        self.snapshot_tree.source_dir)
                if fun_desc.mod else None,
                "glob_var": fun_desc.glob_var,
                "tag": fun_desc.tag
            } for fun_name, fun_desc in g.functions.items()]
        } for group_name, g in self.fun_groups.items()
        ]

        if len(self.fun_groups) == 1 and None in self.fun_groups:
            fun_yaml_dict = fun_yaml_dict[0]["functions"]

        # Create the top level YAML structure.
        yaml_dict = [{
            "diffkemp_version": pkg_resources.require("diffkemp")[0].version,
            "llvm_version": get_llvm_version(),
            "created_time": datetime.datetime.now(datetime.timezone.utc),
            "list_kind": self.list_kind,
            "list": fun_yaml_dict,
            "llvm_source_finder": {
                "kind": self.source_tree.source_finder.str(),
            }
        }]
        if self.store_source_dir:
            yaml_dict[0]["source_dir"] = self.source_tree.source_dir
        return yaml.dump(yaml_dict)
