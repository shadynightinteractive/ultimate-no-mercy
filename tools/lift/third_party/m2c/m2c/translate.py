from __future__ import annotations
import abc
from collections import defaultdict
from contextlib import contextmanager
from dataclasses import dataclass, field, replace
from enum import Enum, auto
import math
import re
import struct
import sys
import traceback
import typing
from typing import (
    AbstractSet,
    Callable,
    DefaultDict,
    Dict,
    Iterator,
    List,
    Mapping,
    Optional,
    Set,
    Tuple,
    Union,
)

from .c_types import ArchC, CType, TypeMap, is_unk_type
from .demangle_codewarrior import parse as demangle_codewarrior_parse, CxxSymbol
from .error import DecompFailure, assert_never
from .flow_graph import (
    ArchFlowGraph,
    BasicNode,
    Block,
    ConditionalNode,
    FlowGraph,
    Function,
    Node,
    InstrRef,
    PrologueRef,
    Reference,
    ReturnNode,
    SwitchNode,
    TerminalNode,
    locs_clobbered_until_dominator,
)
from .ir_pattern import IrPattern, simplify_ir_patterns
from .options import CodingStyle, Formatter, Options, Target
from .asm_file import AsmData, AsmDataEntry, AsmSymbolicData
from .asm_instruction import (
    Argument,
    AsmAddressMode,
    AsmGlobalSymbol,
    AsmLiteral,
    BinOp,
    Macro,
    Register,
    RegisterList,
    Writeback,
)
from .instruction import (
    Instruction,
    InstrProcessingFailure,
    StackLocation,
    Location,
    set_current_instr,
)
from .types import (
    AccessPath,
    ArchAbi,
    ArgLoc,
    FunctionParam,
    FunctionSignature,
    StructDeclaration,
    Type,
    TypeData,
    TypePool,
)

InstrMap = Mapping[str, Callable[["InstrArgs"], "Expression"]]
StmtInstrMap = Mapping[str, Callable[["InstrArgs"], "Statement"]]
CmpInstrMap = Mapping[str, Callable[["InstrArgs"], "Condition"]]
StoreInstrMap = Mapping[str, Callable[["InstrArgs"], Optional["StoreStmt"]]]


class Arch(ArchFlowGraph, ArchC, ArchAbi):
    """Arch-specific information that relates to the translation level.
    Extends ArchFlowGraph."""

    home_space_size: int

    @abc.abstractmethod
    def function_return(self, expr: Expression) -> Dict[Register, Expression]:
        """
        Compute register location(s) & values that will hold the return value
        of the function call `expr`.
        This must have a value for each register in `all_return_regs` in order to stay
        consistent with `Instruction.outputs`. This is why we can't use the
        function's return type, even though it may be more accurate.
        """
        ...

    def is_likely_partial_offset(self, addend: int) -> bool:
        return 0 <= addend < 0x1000000 and addend % 2**15 in (0, 2**15 - 1)

    def c_symbol_name(self, asm_name: str) -> str:
        """Convert an assembler symbol name to its C spelling."""
        return asm_name

    # These are defined here to avoid a circular import in flow_graph.py
    ir_patterns: List[IrPattern] = []

    def simplify_ir(
        self, asm_data: AsmData, flow_graph: FlowGraph, *, debug_patterns: bool
    ) -> None:
        simplify_ir_patterns(
            self, asm_data, flow_graph, self.ir_patterns, debug_patterns=debug_patterns
        )


ASSOCIATIVE_OPS: Set[str] = {"+", "&&", "||", "&", "|", "^", "*"}
COMPOUND_ASSIGNMENT_OPS: Set[str] = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"}
PSEUDO_FUNCTION_OPS: Set[str] = {
    "MULT_HI",
    "MULTU_HI",
    "DMULT_HI",
    "DMULTU_HI",
    "GLUE_F64",
    "CLZ",
}


def as_type(
    expr: Expression, type: Type, silent: bool, *, unify: bool = True
) -> Expression:
    type = type.weaken_void_ptr()
    if isinstance(expr, Literal):
        # In an interesting twist from how types behave everywhere else, for
        # literals we (try to) avoid ever propagating information backwards,
        # by creating a clone of the literal before casting it. This helps
        # avoid casts when literals are used in multiple unrelated places.
        # Importantly, we do keep information about maybe-floatness and
        # 64-bitness of values.
        expr = Literal(expr.value, expr.type.clone_literal_type())
        unify = True
    ptr_target_type = type.get_pointer_target()
    if unify and expr.type.unify(type):
        if silent or isinstance(expr, Literal):
            return expr
    elif ptr_target_type is not None:
        ptr_target_type_size = ptr_target_type.get_size_bytes()
        field_path, field_type, _ = expr.type.get_deref_field(
            0, target_size=ptr_target_type_size
        )
        if field_path is not None and field_type.unify(ptr_target_type):
            expr = AddressOf(
                StructAccess(
                    struct_var=expr,
                    offset=0,
                    target_size=ptr_target_type_size,
                    field_path=field_path,
                    stack_info=None,
                    type=field_type,
                ),
                type=type,
            )
            if silent:
                return expr
    return Cast(expr=expr, reinterpret=True, silent=False, type=type)


def as_var_type(expr: Expression, type: Type, register_cuts: bool) -> Expression:
    """A value as it's assigned to a variable (a register's values joined): as_type, except with --register-cuts (No Arcade), where an
    integer is never cast to a narrower integer type on the way in. A register holds all of what's put in it: a variable typed s16
    because one of its values was loaded with lh still holds a whole word when another value is assigned (func_800F2BBC's s0: lh,
    then the truncated product of floats), and the cast m2c wrote there, `(s16) (s32) (...)`, cut it where no instruction did. The
    variable's declaration is widened afterwards (lift.py to_register_width), so the value stays whole."""
    if register_cuts and expr.type.is_int() and type.is_int() and not expr.type.unify(type):
        return expr
    # (2026-11-08) Also a value whose kind m2c hasn't settled -- a word it reads that might be an int, a pointer or a float
    # (TypeData.K_ANY) -- going into a narrower integer variable: the same cut no instruction made. func_8010F2E4's a0 is a
    # halfword (lhu) on one way and a whole word (lw) on the others, and came out as `var_a0 = (u16) M2C_FIELD(..., s32 *, ...)`
    # where the original passes on the whole word.
    if (
        register_cuts
        and type.is_int()
        and not expr.type.is_float()
        and not expr.type.is_pointer()
        and (expr.type.get_size_bits() or 32) > (type.get_size_bits() or 32)
        and not expr.type.unify(type)
    ):
        return expr
    return as_type(expr, type, silent=True)


def as_f32(expr: Expression) -> Expression:
    return as_type(expr, Type.f32(), True)


def as_f64(expr: Expression) -> Expression:
    return as_type(expr, Type.f64(), True)


def as_sintish(expr: Expression, *, silent: bool = False) -> Expression:
    return as_type(expr, Type.sintish(), silent)


def as_uintish(expr: Expression) -> Expression:
    return as_type(expr, Type.uintish(), False)


def as_s16(expr: Expression) -> Expression:
    return as_type(expr, Type.s16(), False)


def as_u16(expr: Expression) -> Expression:
    return as_type(expr, Type.u16(), False)


def as_u32(expr: Expression) -> Expression:
    return as_type(expr, Type.u32(), False)


def as_s64(expr: Expression, *, silent: bool = False) -> Expression:
    return as_type(expr, Type.s64(), silent)


def as_u64(expr: Expression, *, silent: bool = False) -> Expression:
    return as_type(expr, Type.u64(), silent)


def as_intish(expr: Expression) -> Expression:
    return as_type(expr, Type.intish(), True)


def as_int64(expr: Expression) -> Expression:
    return as_type(expr, Type.int64(), True)


def as_intptr(expr: Expression) -> Expression:
    return as_type(expr, Type.intptr(), True)


def as_ptr(expr: Expression) -> Expression:
    return as_type(expr, Type.ptr(), True)


def as_function_ptr(expr: Expression) -> Expression:
    return as_type(expr, Type.ptr(Type.function()), True)


@dataclass(eq=False)
class PlannedVar:
    """A persistent Var, that can be merged with other ones using a
    union-find-based setup."""

    _parent: Optional[PlannedVar] = None

    def get_representative(self) -> PlannedVar:
        """Find the representative PlannedVar of all the ones that have been
        merged with this one."""
        root = self
        seen = []
        while root._parent is not None:
            seen.append(root)
            root = root._parent
        for var in seen:
            var._parent = root
        return root

    def join(self, other: PlannedVar) -> None:
        a = self.get_representative()
        b = other.get_representative()
        if a != b:
            a._parent = b


class PhiInheritSource(Enum):
    DOMINATOR = auto()
    ANY_PARENT = auto()


@dataclass
class PersistentFunctionState:
    """Function state that's persistent between multiple translation passes."""

    # Instruction outputs that should be assigned to variables. This is computed
    # as part of `assign_naive_phis`, promoting naive phis to planned ones for
    # the next translation pass.
    planned_vars: Dict[Tuple[Register, Reference], PlannedVar] = field(
        default_factory=dict
    )

    # Phi node inputs that can be inherited from the immediate dominator,
    # despite being clobbered on some path to there, or from an arbitrary
    # node predecessor. Similar to planned_vars, this is computed as part of
    # `assign_naive_phis`.
    planned_inherited_phis: Dict[Tuple[Register, Node], PhiInheritSource] = field(
        default_factory=dict
    )


@dataclass
class StackInfo:
    function: Function
    persistent_state: PersistentFunctionState
    global_info: GlobalInfo
    flow_graph: FlowGraph
    warnings: List[str] = field(default_factory=list)
    allocated_stack_size: int = 0
    is_leaf: bool = True
    is_variadic: bool = False
    frame_pointer_reg: Optional[Register] = None
    subroutine_arg_top: int = 0
    callee_save_regs: Set[Register] = field(default_factory=set)
    callee_save_reg_region: Tuple[int, int] = (0, 0)
    unique_type_map: Dict[Tuple[str, object], Type] = field(default_factory=dict)
    local_vars: List[LocalVar] = field(default_factory=list)
    temp_vars: List[Var] = field(default_factory=list)
    naive_phi_vars: List[NaivePhiExpr] = field(default_factory=list)
    reg_vars: Dict[Register, Var] = field(default_factory=dict)
    planned_vars: Dict[Tuple[Register, Reference], Var] = field(default_factory=dict)
    used_reg_vars: Set[Register] = field(default_factory=set)
    arguments: List[PassedInArg] = field(default_factory=list)
    temp_name_counter: Dict[str, int] = field(default_factory=dict)
    nonzero_accesses: Set[Expression] = field(default_factory=set)
    param_names: Dict[ArgLoc, str] = field(default_factory=dict)
    stack_pointer_type: Optional[Type] = None
    replace_first_arg: Optional[Tuple[str, Type]] = None
    weak_stack_var_types: Dict[int, Type] = field(default_factory=dict)
    weak_stack_var_locations: Set[int] = field(default_factory=set)

    def temp_var(self, prefix: str) -> str:
        counter = self.temp_name_counter.get(prefix, 0) + 1
        self.temp_name_counter[prefix] = counter
        return prefix + (f"_{counter}" if counter > 1 else "")

    def in_subroutine_arg_region(self, location: int) -> bool:
        if self.global_info.arch.arch == Target.ArchEnum.PPC:
            return False
        if self.is_leaf:
            return False
        return location < self.subroutine_arg_top

    def in_callee_save_reg_region(self, location: int) -> bool:
        lower_bound, upper_bound = self.callee_save_reg_region
        if lower_bound <= location < upper_bound:
            return True
        # PPC saves LR in the header of the previous stack frame
        if (
            self.global_info.arch.arch == Target.ArchEnum.PPC
            and location == self.allocated_stack_size + 4
        ):
            return True
        return False

    def location_above_stack(self, location: int) -> bool:
        return location >= self.allocated_stack_size

    def add_known_param(self, loc: ArgLoc, name: Optional[str], type: Type) -> Type:
        # A common pattern in C for OOP-style polymorphism involves casting a general "base" struct
        # to a specific "class" struct, where the first member of the class struct is the base struct.
        #
        # For the first argument of the function, if it is a pointer to a base struct, and there
        # exists a class struct named after the first part of the function name, assume that
        # this pattern is being used. Internally, treat the argument as a pointer to the *class*
        # struct, even though it is only a pointer to the *base* struct in the provided context.
        if loc.sort_index == 0 and type.is_pointer() and self.replace_first_arg is None:
            namespace = self.function.name.partition("_")[0]
            base_struct_type = type.get_pointer_target()
            self_struct = self.global_info.typepool.get_struct_by_tag_name(
                namespace, self.global_info.arch, self.global_info.typemap
            )
            if (
                self_struct is not None
                and base_struct_type is not None
                and base_struct_type.is_struct()
            ):
                # Check if `self_struct_type` contains a `base_struct_type` at offset 0
                self_struct_type = Type.struct(self_struct)
                field_path, field_type, _ = self_struct_type.get_field(
                    offset=0, target_size=base_struct_type.get_size_bytes()
                )
                if (
                    field_path is not None
                    and field_type.unify(base_struct_type)
                    and not self_struct_type.unify(base_struct_type)
                ):
                    # Success, it looks like `self_struct_type` extends `base_struct_type`.
                    # By default, name the local var `self`, unless the argument name is `thisx` then use `this`
                    self.replace_first_arg = (name or "_self", type)
                    name = "this" if name == "thisx" else "self"
                    type = Type.ptr(Type.struct(self_struct))
        if name:
            self.param_names[loc] = name
        arg = self.get_argument(loc)
        self.arguments.append(arg)
        self.arguments.sort(key=lambda a: a.loc)
        arg.type.unify(type)
        return type

    def get_param_name(self, loc: ArgLoc) -> Optional[str]:
        return self.param_names.get(loc)

    def add_local_var(self, var: LocalVar) -> None:
        if any(v.value == var.value for v in self.local_vars):
            return
        self.local_vars.append(var)
        # Make sure the local vars stay sorted in order on the stack.
        self.local_vars.sort(key=lambda v: v.value)

    def add_argument(self, arg: PassedInArg) -> None:
        for a in self.arguments:
            if a.loc == arg.loc:
                # If the first mention of the argument is by taking a pointer
                # (e.g. from a loop over a stack array), and we only then read
                # from a register, refine saved location info.
                if a.loc.reg is None and arg.loc.reg is not None:
                    self.arguments.remove(a)
                    break
                return
        self.arguments.append(arg)
        self.arguments.sort(key=lambda a: a.loc)

    def get_argument(self, loc: ArgLoc) -> PassedInArg:
        return PassedInArg(
            loc,
            stack_info=self,
            type=self.unique_type_for("arg", loc, Type.any_reg()),
        )

    def get_argument_above_stack(
        self, offset: int, size: Optional[int] = None
    ) -> Tuple[Expression, PassedInArg]:
        aligned_offset = offset & -4
        loc = ArgLoc(aligned_offset, None, None)
        arg = self.get_argument(loc)
        # (No Mercy Arcade, 2026-11-08) A read of part of a parameter's word other than its low byte or low half: the bits the
        # instruction reads, shifted down -- the word is big-endian, so the byte at +1 is bits 16-23. m2c gave the whole
        # parameter for any of these (and for a byte or half at +0), and a byte read elsewhere at +3 then made it a u8, so
        # func_801446C4's `lbu 0x3D($sp)`, `lbu 0x3E($sp)` and `lw 0x3C($sp)` all came out as its low byte. Only for a read whose
        # size is known (size: from deref); the cases m2c had right -- the low byte, the low half, the whole word -- are as before.
        if size in (1, 2) and (offset - aligned_offset) + size < 4:
            shift = 8 * (4 - size - (offset - aligned_offset))
            part = BinaryOp(
                left=as_type(arg, Type.intish(), True),
                op=">>",
                right=Literal(shift),
                type=Type.u32(),
            )
            return as_type(part, Type.int_of_size(8 * size), False), arg
        if aligned_offset == offset - 3:
            return as_type(arg, Type.int_of_size(8), True), arg
        if aligned_offset == offset - 2:
            return as_type(arg, Type.int_of_size(16), True), arg
        return arg, arg

    def record_struct_access(self, ptr: Expression, location: int) -> None:
        if location:
            self.nonzero_accesses.add(unwrap_deep(ptr))

    def has_nonzero_access(self, ptr: Expression) -> bool:
        return unwrap_deep(ptr) in self.nonzero_accesses

    def unique_type_for(self, category: str, key: object, default: Type) -> Type:
        key = (category, key)
        if key not in self.unique_type_map:
            self.unique_type_map[key] = default
        return self.unique_type_map[key]

    def saved_reg_symbol(self, reg_name: str) -> GlobalSymbol:
        sym_name = "saved_reg_" + reg_name
        type = self.unique_type_for("saved_reg", sym_name, Type.any_reg())
        return GlobalSymbol(c_symbol_name=sym_name, type=type)

    def should_save(self, expr: Expression, offset: Optional[int]) -> bool:
        expr = early_unwrap(expr)
        if isinstance(expr, GlobalSymbol) and (
            expr.c_symbol_name.startswith("saved_reg_") or expr.c_symbol_name == "sp"
        ):
            return True
        if (
            isinstance(expr, PassedInArg)
            and expr.loc.offset is not None
            and (
                offset is None or offset == self.allocated_stack_size + expr.loc.offset
            )
        ):
            return True
        return False

    def get_stack_var(self, location: int, *, store: bool, size: Optional[int] = None) -> Expression:
        # See `get_stack_info` for explanation
        if self.in_callee_save_reg_region(location):
            # Some annoying bookkeeping instruction. To avoid
            # further special-casing, just return whatever - it won't matter.
            return LocalVar(location, type=Type.any_reg(), path=None)
        elif self.location_above_stack(location):
            offset = location - self.allocated_stack_size
            # (size only for a read: a store to part of a parameter's slot is as m2c had it.)
            ret, arg = self.get_argument_above_stack(offset, None if store else size)
            if not store:
                self.add_argument(arg)
            return ret
        elif self.in_subroutine_arg_region(location):
            return SubroutineArg(location, type=Type.any_reg())
        else:
            # Local variable
            assert self.stack_pointer_type is not None
            field_path, field_type, _ = self.stack_pointer_type.get_deref_field(
                location, target_size=None
            )

            # Some variables on the stack are compiler-managed, and aren't declared
            # in the original source. These variables can have different types inside
            # different blocks, so we track their types but assume that they may change
            # on each store.
            # TODO: Because the types are tracked in StackInfo instead of RegInfo, it is
            # possible that a load could incorrectly use a weak type from a sibling node
            # instead of a parent node. A more correct implementation would use similar
            # logic to the phi system. In practice however, storing types in StackInfo
            # works well enough because nodes are traversed approximately depth-first.
            # TODO: Maybe only do this for certain configurable regions?

            # Get the previous type stored in `location`
            previous_stored_type = self.weak_stack_var_types.get(location)
            if previous_stored_type is not None:
                # Check if the `field_type` is compatible with the type of the last store
                if not previous_stored_type.unify(field_type):
                    # The types weren't compatible: mark this `location` as "weak"
                    # This marker is only used to annotate the output
                    self.weak_stack_var_locations.add(location)

                if store:
                    # If there's already been a store to `location`, then return a fresh type
                    field_type = Type.any_field()
                else:
                    # Use the type of the last store instead of the one from `get_deref_field()`
                    field_type = previous_stored_type

            # Track the type last stored at `location`
            if store:
                self.weak_stack_var_types[location] = field_type

            return LocalVar(location, type=field_type, path=field_path)

    def maybe_get_register_var(self, reg: Register) -> Optional[Var]:
        return self.reg_vars.get(reg)

    def add_register_var(self, reg: Register, name: str) -> None:
        type = Type.floatish() if reg.is_float() else Type.intptr()
        var = Var(self, prefix=f"var_{name}", is_planned=True, type=type)
        self.reg_vars[reg] = var
        self.temp_vars.append(var)

    def get_planned_var(self, reg: Register, source: Reference) -> Optional[Var]:
        # Ignore reg_vars for function calls and initial argument registers, to
        # avoid clutter.
        var = self.reg_vars.get(reg)
        if (
            var
            and isinstance(source, InstrRef)
            and source.instruction.function_target is None
        ):
            return var
        return self.planned_vars.get((reg, source))

    def get_persistent_planned_var(
        self, reg: Register, source: Reference
    ) -> PlannedVar:
        ret = self.persistent_state.planned_vars.get((reg, source))
        if not ret:
            ret = PlannedVar()
            self.persistent_state.planned_vars[(reg, source)] = ret
        return ret

    def add_planned_inherited_phi(
        self, node: Node, reg: Register, source: PhiInheritSource
    ) -> None:
        self.persistent_state.planned_inherited_phis[(reg, node)] = source

    def get_planned_inherited_phi(
        self, node: Node, reg: Register
    ) -> Optional[PhiInheritSource]:
        return self.persistent_state.planned_inherited_phis.get((reg, node))

    def is_stack_reg(self, reg: Register) -> bool:
        if reg == self.global_info.arch.stack_pointer_reg:
            return True
        if reg == self.frame_pointer_reg:
            return True
        return False

    def get_struct_type_map(self) -> Dict[Expression, Dict[int, Type]]:
        """Reorganize struct information in unique_type_map by var & offset"""
        struct_type_map: Dict[Expression, Dict[int, Type]] = {}
        for (category, key), type in self.unique_type_map.items():
            if category != "struct":
                continue
            var, offset = typing.cast(Tuple[Expression, int], key)
            if var not in struct_type_map:
                struct_type_map[var] = {}
            struct_type_map[var][offset] = type
        return struct_type_map

    def __str__(self) -> str:
        return "\n".join(
            [
                f"Stack info for function {self.function.name}:",
                f"Allocated stack size: {self.allocated_stack_size}",
                f"Leaf? {self.is_leaf}",
                f"Bounds of callee-saved vars region: {self.callee_save_reg_region}",
                f"Subroutine arg top: {self.subroutine_arg_top}",
                f"Callee save registers: {self.callee_save_regs}",
            ]
        )


def get_stack_info(
    function: Function,
    persistent_state: PersistentFunctionState,
    global_info: GlobalInfo,
    flow_graph: FlowGraph,
) -> StackInfo:
    arch = global_info.arch
    info = StackInfo(function, persistent_state, global_info, flow_graph)

    # The goal here is to pick out special instructions that provide information
    # about this function's stack setup.
    #
    # IDO puts local variables *above* the saved registers on the stack, but
    # GCC puts local variables *below* the saved registers.
    # To support both, we explicitly determine both the upper & lower bounds of the
    # saved registers. Then, we estimate the boundary of the subroutine arguments
    # by finding the lowest stack offset that is loaded from or computed. (This
    # assumes that the compiler will never reuse a section of stack for *both*
    # a local variable *and* a subroutine argument.) Anything within the stack frame,
    # but outside of these two regions, is considered a local variable.
    callee_saved_offsets: List[int] = []
    allowed_callee_save_gaps: Set[int] = set()
    # Track simple literal values stored into registers: MIPS compilers need a temp
    # reg to move the stack pointer more than 0x7FFF bytes.
    temp_reg_values: Dict[Register, int] = {}
    for inst in flow_graph.entry_node().block.instructions:
        arch_mnemonic = inst.arch_mnemonic(arch)
        if inst.function_target:
            break
        elif arch_mnemonic == "mips:addiu" and inst.args[0] == arch.stack_pointer_reg:
            # Moving the stack pointer on MIPS
            assert isinstance(inst.args[2], AsmLiteral)
            info.allocated_stack_size = abs(inst.args[2].as_s16())
        elif (
            arch_mnemonic == "mips:subu"
            and inst.args[0] == arch.stack_pointer_reg
            and inst.args[1] == arch.stack_pointer_reg
            and inst.args[2] in temp_reg_values
        ):
            # Moving the stack pointer more than 0x7FFF on MIPS
            # TODO: This instruction needs to be ignored later in translation, in the
            # same way that `addiu $sp, $sp, N` is ignored in handle_addi_real
            assert isinstance(inst.args[2], Register)
            info.allocated_stack_size = temp_reg_values[inst.args[2]]
        elif arch_mnemonic == "ppc:stwu" and inst.args[0] == arch.stack_pointer_reg:
            # Moving the stack pointer on PPC
            assert isinstance(inst.args[1], AsmAddressMode)
            assert isinstance(inst.args[1].addend, AsmLiteral)
            info.allocated_stack_size = abs(inst.args[1].addend.value)
        elif arch_mnemonic in ("arm:push", "arm:vpush"):
            assert isinstance(inst.args[0], RegisterList)
            for reg in inst.args[0].regs[::-1]:
                tp, _ = reg.arm_regtype_index()
                num_words = {"r": 1, "s": 1, "d": 2, "q": 4}[tp]
                for i in range(num_words):
                    info.allocated_stack_size += 4
                    if reg in arch.saved_regs:
                        callee_saved_offsets.append(-info.allocated_stack_size)
                if reg in arch.saved_regs:
                    info.callee_save_regs.add(reg)
                if reg == Register("lr"):
                    info.is_leaf = False
        elif (
            arch_mnemonic in ("arm:sub", "arm:add")
            and inst.args[0] == arch.stack_pointer_reg
            and inst.args[1] == arch.stack_pointer_reg
            and isinstance(inst.args[2], AsmLiteral)
        ):
            imm = inst.args[2].value * (-1 if arch_mnemonic == "arm:add" else 1)
            info.allocated_stack_size += max(imm, 0)
        elif (
            arch_mnemonic in ("mips:move", "arm:mov", "ppc:mr")
            and isinstance(inst.args[0], Register)
            and inst.args[0] in arch.frame_pointer_regs
            and inst.args[1] == arch.stack_pointer_reg
        ):
            # "move fp, sp" very likely means the code is compiled with frame
            # pointers enabled; thus fp should be treated the same as sp.
            info.frame_pointer_reg = inst.args[0]
        elif (
            arch_mnemonic == "sh:mov"
            and inst.args[0] == arch.stack_pointer_reg
            and isinstance(inst.args[1], Register)
            and inst.args[1] in arch.frame_pointer_regs
        ):
            info.frame_pointer_reg = inst.args[1]
        elif (
            arch_mnemonic == "sh:add"
            and isinstance(inst.args[0], AsmLiteral)
            and inst.args[0].as_s8() < 0
            and inst.args[1] == arch.stack_pointer_reg
        ):
            info.allocated_stack_size += -inst.args[0].as_s8()
        elif (
            arch_mnemonic in ("sh:mov.l", "sh:sts.l")
            and isinstance(inst.args[0], Register)
            and inst.args[0] in arch.saved_regs
            and isinstance(inst.args[1], AsmAddressMode)
            and inst.args[1].base == arch.stack_pointer_reg
            and inst.args[1].writeback == Writeback.PRE
        ):
            info.allocated_stack_size += 4
            info.callee_save_regs.add(inst.args[0])
            callee_saved_offsets.append(-info.allocated_stack_size)
            if inst.args[0] == arch.return_address_reg:
                info.is_leaf = False
        elif (
            arch_mnemonic
            in [
                "mips:sw",
                "mips:sd",
                "mips:sq",
                "mips:swc1",
                "mips:sdc1",
                "ppc:stw",
                "ppc:stmw",
                "ppc:stfd",
                "ppc:psq_st",
            ]
            and isinstance(inst.args[0], Register)
            and inst.args[0] in arch.saved_regs
            and isinstance(inst.args[1], AsmAddressMode)
            and inst.args[1].base == arch.stack_pointer_reg
            and (
                inst.args[0] not in info.callee_save_regs
                or arch_mnemonic == "ppc:psq_st"
            )
        ):
            # Initial saving of callee-save register onto the stack.
            if inst.args[0] in (arch.return_address_reg, Register("r0")):
                # Saving the return address on the stack.
                info.is_leaf = False
            # The registers & their stack accesses must be matched up in ArchAsm.parse
            for inp, mem in zip(inst.inputs, inst.outputs):
                if isinstance(inp, Register) and isinstance(mem, StackLocation):
                    assert mem.symbolic_offset is None
                    stack_offset = mem.offset
                    if arch_mnemonic != "ppc:psq_st":
                        # psq_st instructions store the same register as stfd, just
                        # as packed singles instead. Prioritize the stfd.
                        info.callee_save_regs.add(inp)
                    callee_saved_offsets.append(stack_offset)
                    if arch_mnemonic == "mips:sd":
                        # Some PS2 compilers use sd/ld to save/restore registers, but
                        # reserve 0x10 bytes of space as if they had used sq/lq.
                        # Allow for the discrepancy.
                        allowed_callee_save_gaps.add(stack_offset + 8)
                    if arch_mnemonic == "mips:swc1":
                        # Similarly, they use swc1 but reserve 8 bytes of space.
                        allowed_callee_save_gaps.add(stack_offset + 4)
        elif arch_mnemonic == "ppc:mflr" and inst.args[0] == Register("r0"):
            info.is_leaf = False
        elif arch_mnemonic == "mips:li" and inst.args[0] in arch.temp_regs:
            assert isinstance(inst.args[0], Register)
            assert isinstance(inst.args[1], AsmLiteral)
            temp_reg_values[inst.args[0]] = inst.args[1].value
        elif (
            arch_mnemonic == "mips:ori"
            and inst.args[0] == inst.args[1]
            and inst.args[0] in temp_reg_values
        ):
            assert isinstance(inst.args[0], Register)
            assert isinstance(inst.args[2], AsmLiteral)
            temp_reg_values[inst.args[0]] |= inst.args[2].value

    if arch.arch in (Target.ArchEnum.ARM, Target.ArchEnum.SH):
        # On ARM and SH we don't know the stack size up front, so
        # callee_saved_offsets needs to be adjusted after scanning the full first block.
        for i in range(len(callee_saved_offsets)):
            callee_saved_offsets[i] += info.allocated_stack_size

    if not info.is_leaf:
        # Iterate over the whole function, not just the first basic block,
        # to estimate the boundary for the subroutine argument region
        info.subroutine_arg_top = info.allocated_stack_size
        for node in flow_graph.nodes:
            for inst in node.block.instructions:
                arch_mnemonic = inst.arch_mnemonic(arch)
                # TODO: it would be nice if we could scan inst.output for StackLocation
                # dependencies here. However, that's not yet implemented for all
                # architectures, and at the point where those are created, we don't
                # know if we are using frame pointers... For now, assume that any
                # AsmAddressMode we see in a load instruction is where we load from.
                if inst.is_load:
                    for arg in inst.args:
                        if (
                            isinstance(arg, AsmAddressMode)
                            and info.is_stack_reg(arg.base)
                            and isinstance(arg.addend, AsmLiteral)
                        ):
                            offset = arg.addend.value & ~3
                            if offset >= arch.home_space_size:
                                info.subroutine_arg_top = min(
                                    info.subroutine_arg_top, offset
                                )
                # TODO: do this for sh2 too (more annoying since it involves two
                # instructions: "mov sp, reg; add imm, reg")
                if (
                    arch_mnemonic in ("mips:addiu", "ppc:addi", "arm:add")
                    and isinstance(inst.args[1], Register)
                    and info.is_stack_reg(inst.args[1])
                    and isinstance(inst.args[0], Register)
                    and not info.is_stack_reg(inst.args[0])
                    and isinstance(inst.args[2], AsmLiteral)
                    and inst.args[2].value < info.allocated_stack_size
                ):
                    info.subroutine_arg_top = min(
                        info.subroutine_arg_top, inst.args[2].value
                    )
                if (
                    arch_mnemonic == "arm:mov"
                    and isinstance(inst.args[1], Register)
                    and info.is_stack_reg(inst.args[1])
                    and isinstance(inst.args[0], Register)
                    and not info.is_stack_reg(inst.args[0])
                ):
                    info.subroutine_arg_top = 0

    # Compute the bounds of the callee-saved register region, including padding
    if callee_saved_offsets:
        callee_saved_offsets.sort()
        bottom = callee_saved_offsets[0]

        # Both IDO & GCC save registers in two subregions:
        # (a) One for double-sized registers
        # (b) One for word-sized registers, padded to a multiple of 8 bytes
        # IDO has (a) lower than (b); GCC has (b) lower than (a)
        # Check that there are no gaps in this region, other than a single
        # 4-byte word between subregions.
        top = bottom
        internal_padding_added = False
        has_warned = False
        for offset in callee_saved_offsets:
            while top < offset and top in allowed_callee_save_gaps:
                top += 4
            if offset != top:
                if not internal_padding_added and offset == top + 4:
                    internal_padding_added = True
                elif not has_warned:
                    has_warned = True
                    info.warnings.append(
                        f"Gap in callee-saved word stack region.\n"
                        f"Saved: [{', '.join(hex(o) for o in callee_saved_offsets)}], "
                        f"gap at: {hex(top)}."
                    )
            top = offset + 4
        info.callee_save_reg_region = (bottom, top)

        # Subroutine arguments must be at the very bottom of the stack, so they
        # must come after the callee-saved region
        info.subroutine_arg_top = min(info.subroutine_arg_top, bottom)

    # Use a struct to represent the stack layout. If the struct is provided in the context,
    # its fields will be used for variable types & names.
    stack_struct_name = f"_m2c_stack_{function.name}"
    stack_struct = global_info.typepool.get_struct_by_tag_name(
        stack_struct_name, global_info.arch, global_info.typemap
    )
    if stack_struct is not None:
        if stack_struct.size != info.allocated_stack_size:
            raise DecompFailure(
                f"Function {function.name} has a provided stack type {stack_struct_name} "
                f"with size {stack_struct.size}, but the detected stack size was "
                f"{info.allocated_stack_size}."
            )
    else:
        stack_struct = StructDeclaration.unknown(
            global_info.typepool,
            size=info.allocated_stack_size,
            tag_name=stack_struct_name,
        )
    # Mark the struct as a stack struct so we never try to use a reference to the struct itself
    stack_struct.is_stack = True
    stack_struct.new_field_prefix = "sp"

    # This acts as the type of the $sp register
    info.stack_pointer_type = Type.ptr(Type.struct(stack_struct))

    return info


def format_hex(val: int) -> str:
    return format(val, "x").upper()


def escape_byte(b: int) -> bytes:
    table = {
        b"\0": b"\\0",
        b"\b": b"\\b",
        b"\f": b"\\f",
        b"\n": b"\\n",
        b"\r": b"\\r",
        b"\t": b"\\t",
        b"\v": b"\\v",
        b"\\": b"\\\\",
        b'"': b'\\"',
    }
    bs = bytes([b])
    if bs in table:
        return table[bs]
    if b < 0x20 or b in (0xFF, 0x7F):
        return f"\\x{b:02x}".encode("ascii")
    return bs


@dataclass(eq=False)
class Var:
    stack_info: StackInfo = field(repr=False)
    prefix: str
    is_planned: bool
    type: Type
    is_emitted: bool = False
    name: Optional[str] = None
    debug_exprs: List[Expression] = field(repr=False, default_factory=list)

    def format(self, fmt: Formatter) -> str:
        # Assign names lazily, hopefully in approximate output source order.
        assert self.is_emitted
        if self.name is None:
            self.name = self.stack_info.temp_var(self.prefix)
        return self.name

    def __str__(self) -> str:
        return f"<var: {id(self)}>"


class Expression(abc.ABC):
    type: Type

    @abc.abstractmethod
    def dependencies(self) -> List[Expression]: ...

    def use(self) -> None:
        """Mark an expression as "will occur in the output". Various subclasses
        override this to provide special behavior; for instance, EvalOnceExpr
        checks if it occurs more than once in the output and if so emits a temp.
        It is important to get the number of use() calls correct:
        * if use() is called but the expression is not emitted, it may cause
          function calls to be silently dropped.
        * if use() is not called but the expression is emitted, it may cause phi
          variables to be printed as unnamed-phi($reg), without any assignment
          to that phi.
        * if use() is called once but the expression is emitted twice, it may
          cause function calls to be duplicated."""
        for expr in self.dependencies():
            expr.use()

    @abc.abstractmethod
    def format(self, fmt: Formatter) -> str: ...

    def __str__(self) -> str:
        """Stringify an expression for debug purposes. The output can change
        depending on when this is called, e.g. because of EvalOnceExpr state.
        To avoid using it by accident, output is quoted."""
        fmt = Formatter(debug=True)
        return '"' + self.format(fmt) + '"'


class Condition(Expression):
    @abc.abstractmethod
    def negated(self) -> Condition: ...


class Statement(abc.ABC):
    @abc.abstractmethod
    def should_write(self) -> bool: ...

    @abc.abstractmethod
    def format(self, fmt: Formatter) -> str: ...

    def __str__(self) -> str:
        """Stringify a statement for debug purposes. The output can change
        depending on when this is called, e.g. because of EvalOnceExpr state.
        To avoid using it by accident, output is quoted."""
        fmt = Formatter(debug=True)
        return '"' + self.format(fmt) + '"'


@dataclass(frozen=True, eq=False)
class ErrorExpr(Condition):
    desc: Optional[str] = None
    type: Type = field(default_factory=Type.any_reg)

    def dependencies(self) -> List[Expression]:
        return []

    def negated(self) -> Condition:
        return self

    def format(self, fmt: Formatter) -> str:
        if self.desc is not None:
            return f"M2C_ERROR(/* {self.desc} */)"
        return "M2C_ERROR()"


@dataclass(frozen=True)
class CommentExpr(Expression):
    expr: Expression
    type: Type = field(compare=False)
    prefix: Optional[str] = None
    suffix: Optional[str] = None

    def dependencies(self) -> List[Expression]:
        return [self.expr]

    def format(self, fmt: Formatter) -> str:
        expr_str = self.expr.format(fmt)

        if fmt.coding_style.comment_style == CodingStyle.CommentStyle.NONE:
            return expr_str

        prefix_str = f"/* {self.prefix} */ " if self.prefix is not None else ""
        suffix_str = f" /* {self.suffix} */" if self.suffix is not None else ""
        return f"{prefix_str}{expr_str}{suffix_str}"

    @staticmethod
    def wrap(
        expr: Expression, prefix: Optional[str] = None, suffix: Optional[str] = None
    ) -> Expression:
        if prefix is None and suffix is None:
            return expr
        return CommentExpr(expr=expr, type=expr.type, prefix=prefix, suffix=suffix)


@dataclass(frozen=True, eq=False)
class SecondF64Half(Expression):
    type: Type = field(default_factory=Type.any_reg)

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter) -> str:
        return "(second half of f64)"


@dataclass(frozen=True, eq=False)
class CarryBit(Expression):
    expr: Optional[Expression] = None
    type: Type = field(default_factory=Type.intish)

    def dependencies(self) -> List[Expression]:
        return [self.expr] if self.expr is not None else []

    def format(self, fmt: Formatter) -> str:
        if self.expr is not None:
            return f"M2C_CARRY({self.expr.format(fmt)})"
        return "M2C_CARRY"


@dataclass(frozen=True, eq=False)
class BinaryOp(Condition):
    left: Expression
    op: str
    right: Expression
    type: Type
    # (No Mercy Arcade, 2026-11-07: "S" or "U" for a compare made from a signed or an unsigned instruction -- slt, bltz, blez...
    #  or sltu, sltiu -- kept through negation and the other rewrites of a condition. With --compare-types the compare is
    #  written so C++ compares in that signedness whatever its operands' types turn out to be: see format_compare.)
    cmp: Optional[str] = field(default=None, compare=False)
    # (No Mercy Arcade, 2026-11-08: a signed division m2c made from a multiply by a magic number (evaluate.py fold_divmod),
    #  whose sign correction hasn't been found yet. Until then the instructions' value is the quotient less one for a negative
    #  dividend -- one less than C's x / d -- and that is what it prints as: see format_floor_division.)
    floor_div: bool = field(default=False, compare=False)
    # (No Mercy Arcade, 2026-11-12, agent "refused": "S" or "U" for the quotient or remainder of a div or divu instruction
    #  itself. With --n64-division it is written LIFT_DIV(a, b) and the like: see N64_DIVISION.)
    div: Optional[str] = field(default=None, compare=False)

    @staticmethod
    def int(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_intish(left), op=op, right=as_intish(right), type=Type.intish()
        )

    @staticmethod
    def int64(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_int64(left), op=op, right=as_int64(right), type=Type.int64()
        )

    @staticmethod
    def intptr(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_intptr(left), op=op, right=as_intptr(right), type=Type.intptr()
        )

    @staticmethod
    def icmp(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_intptr(left), op=op, right=as_intptr(right), type=Type.boolean()
        )

    @staticmethod
    def scmp(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_sintish(left, silent=True),
            op=op,
            right=as_sintish(right, silent=True),
            type=Type.boolean(),
            cmp="S",
        )

    @staticmethod
    def sintptr_cmp(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_type(left, Type.sintptr(), False),
            op=op,
            right=as_type(right, Type.sintptr(), False),
            type=Type.boolean(),
            cmp="S",
        )

    @staticmethod
    def ucmp(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_uintish(left), op=op, right=as_uintish(right), type=Type.boolean(),
            cmp="U",
        )

    @staticmethod
    def uintptr_cmp(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_type(left, Type.uintptr(), False),
            op=op,
            right=as_type(right, Type.uintptr(), False),
            type=Type.boolean(),
            cmp="U",
        )

    @staticmethod
    def fcmp(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_f32(left),
            op=op,
            right=as_f32(right),
            type=Type.boolean(),
        )

    @staticmethod
    def dcmp(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_f64(left),
            op=op,
            right=as_f64(right),
            type=Type.boolean(),
        )

    @staticmethod
    def sint(
        left: Expression, op: str, right: Expression, *, silent: bool = False
    ) -> BinaryOp:
        return BinaryOp(
            left=as_sintish(left, silent=silent),
            op=op,
            right=as_sintish(right, silent=silent),
            type=Type.s32(),
        )

    @staticmethod
    def uint(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_uintish(left), op=op, right=as_uintish(right), type=Type.u32()
        )

    @staticmethod
    def sshift(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_sintish(left), op=op, right=as_intish(right), type=Type.s32()
        )

    @staticmethod
    def ushift(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_uintish(left), op=op, right=as_intish(right), type=Type.u32()
        )

    @staticmethod
    def s64(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(left=as_s64(left), op=op, right=as_s64(right), type=Type.s64())

    @staticmethod
    def u64(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(left=as_u64(left), op=op, right=as_u64(right), type=Type.u64())

    @staticmethod
    def f32(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_f32(left),
            op=op,
            right=as_f32(right),
            type=Type.f32(),
        )

    @staticmethod
    def f64(left: Expression, op: str, right: Expression) -> BinaryOp:
        return BinaryOp(
            left=as_f64(left),
            op=op,
            right=as_f64(right),
            type=Type.f64(),
        )

    def is_comparison(self) -> bool:
        return self.op in ["==", "!=", ">", "<", ">=", "<="]

    def is_floating(self) -> bool:
        return self.left.type.is_float() and self.right.type.is_float()

    def negated(self) -> Condition:
        if (
            self.op in ["&&", "||"]
            and isinstance(self.left, Condition)
            and isinstance(self.right, Condition)
        ):
            # DeMorgan's Laws
            return BinaryOp(
                left=self.left.negated(),
                op={"&&": "||", "||": "&&"}[self.op],
                right=self.right.negated(),
                type=Type.boolean(),
            )
        if not self.is_comparison() or (
            self.is_floating() and self.op in ["<", ">", "<=", ">="]
        ):
            # Floating-point comparisons cannot be negated in any nice way,
            # due to nans.
            return UnaryOp("!", self, type=Type.boolean())
        return BinaryOp(
            left=self.left,
            op={"==": "!=", "!=": "==", ">": "<=", "<": ">=", ">=": "<", "<=": ">"}[
                self.op
            ],
            right=self.right,
            type=Type.boolean(),
            cmp=self.cmp,
        )

    def dependencies(self) -> List[Expression]:
        return [self.left, self.right]

    def normalize_for_formatting(self) -> BinaryOp:
        right_expr = late_unwrap(self.right)
        if (
            not self.is_floating()
            and isinstance(right_expr, Literal)
            and right_expr.value < 0
        ):
            if self.op == "+":
                neg = Literal(value=-right_expr.value, type=right_expr.type)
                sub = BinaryOp(op="-", left=self.left, right=neg, type=self.type)
                return sub
            if self.op in ("&", "|"):
                neg = Literal(value=~right_expr.value, type=right_expr.type)
                right = UnaryOp("~", neg, type=Type.any_reg())
                expr = BinaryOp(op=self.op, left=self.left, right=right, type=self.type)
                return expr
        return self

    def format(self, fmt: Formatter) -> str:
        left_expr = late_unwrap(self.left)
        right_expr = late_unwrap(self.right)

        adj = self.normalize_for_formatting()
        if adj is not self:
            return adj.format(fmt)

        if (
            self.is_comparison()
            and isinstance(left_expr, Literal)
            and not isinstance(right_expr, Literal)
        ):
            return BinaryOp(
                left=right_expr,
                op=self.op.translate(str.maketrans("<>", "><")),
                right=left_expr,
                type=self.type,
                cmp=self.cmp,
            ).format(fmt)

        # For comparisons to a Literal, cast the Literal to the type of the lhs
        # (This is not done with complex expressions to avoid propagating incorrect
        # type information: end-of-array pointers are particularly bad.)
        if self.op in ("==", "!=") and isinstance(right_expr, Literal):
            right_expr = elide_literal_casts(as_type(right_expr, left_expr.type, True))

        # For commutative, left-associative operations, strip unnecessary parentheses.
        lhs = left_expr.format(fmt)
        if (
            isinstance(left_expr, BinaryOp)
            and left_expr.op == self.op
            and self.op in ASSOCIATIVE_OPS
        ):
            lhs = lhs[1:-1]

        # For certain operators, use base-10 (decimal) for the RHS
        if self.op in ("/", "%") and isinstance(right_expr, Literal):
            rhs = right_expr.format(fmt, force_dec=True)
        else:
            rhs = right_expr.format(fmt)

        # These aren't real operators (or functions); format them as a fn call
        if self.op in PSEUDO_FUNCTION_OPS:
            return f"{self.op}({lhs}, {rhs})"

        if N64_DIVISION and self.div is not None and self.op in ("/", "%"):
            # (--n64-division: N64_DIVISION, above. The operands whole, as a call's arguments.)
            name = ("LIFT_DIV" if self.op == "/" else "LIFT_REM") + ("U" if self.div == "U" else "")
            return f"{name}({format_expr(left_expr, fmt)}, {format_expr(right_expr, fmt)})"

        if COMPARE_TYPES and self.cmp is not None and self.op in ("<", ">", "<=", ">="):
            lhs, rhs = format_compare(self.cmp, left_expr, lhs, right_expr, rhs, fmt)

        if self.floor_div and self.op == "/":
            return format_floor_division(lhs, rhs)

        return f"({lhs} {self.op} {rhs})"


def format_floor_division(lhs: str, rhs: str) -> str:
    """No Mercy Arcade (2026-11-08): a signed division by a constant as the N64 compiler's multiply-by-a-magic-number leaves it
    before its sign correction (BinaryOp.floor_div): C's quotient, less one when the dividend is negative -- `(x / d) +
    (x >> 31)`. GCC subtracts the dividend's sign (x >> 31) afterwards, and m2c folds that pair into x / d where it finds both in
    one expression (fold_divmod); where it doesn't, the C has to say what the multiply gave. func_80118F74 divides on two ways into
    one join and subtracts the sign after it: `var_v1 = (arg1 << 8) / 30` on one way, `var_a0 = (arg1 << 8) >> 31` beside it,
    then `var_v1 - var_a0`, which was one too many for every negative dividend. The dividend is written twice, so not where it
    calls a function (that would call it twice): an M2C_ERROR then, and lift.py refuses the function."""
    if re.search(r"\b(?:func_\w+|lift_overlay_[0-9A-F]{8}|M2C_ERROR)\(", lhs):
        return "M2C_ERROR(/* a magic-number division with its sign correction elsewhere, of a call's result */)"
    return f"(({lhs} / {rhs}) + ((s32) {lhs} >> 31))"


# No Mercy Arcade, --compare-types (2026-11-07): set by main.run.
COMPARE_TYPES = False
# (No Mercy Arcade, --n64-division, 2026-11-12, agent "refused"): a div or divu with none of the compiler's checks after it --
# hand-written code, rw_80118A20's `div $zero, $s2, $s4` -- divides on the N64 whatever its operands: by zero it gives the
# quotient -1 (1 for a negative number) and the number itself as the remainder, and the most negative number by -1 gives
# itself and 0. C++'s / and % stop the PC there instead (lift.py refused such a function: "divides by a variable"). Each such
# division is written LIFT_DIV(a, b), LIFT_REM, LIFT_DIVU or LIFT_REMU (lift_mem.h), which give the N64's results in every
# case -- the ones the tester's originals give too (fuzz.h FUZZ_DIV). lift.py asks for it only for such a function.
N64_DIVISION = False
# (No Mercy Arcade, --n64-conversion, 2026-11-13, the ARM64 proof): a float converted to a word -- trunc.w.s, trunc.w.d -- was
# written as a C cast, `(s32) x`. Out of the 32-bit range, for an infinity or a NaN that cast has no defined result in C++: x86
# gives 0x80000000 (cvttss2si), ARM64 gives the nearest end of the range and 0 for a NaN (fcvtzs), and a compiler may fold it to
# anything. (The N64 itself stops there: the VR4300 raises its unimplemented-operation exception.) With the option each one is
# written LIFT_TRUNC_W_S(x) / LIFT_TRUNC_W_D(x) (lift_mem.h), which give the conversion's result in range and 0x80000000
# otherwise on every CPU -- what the x86 build has always done, so no proved result changes there. Set by main.run.
N64_CONVERSION = False


def c_sign_of_type(t: Type, declared: bool) -> Optional[str]:
    """How C++ compares a value of this m2c type, after promotion: "S" as int, "U" as unsigned int, "P" a game pointer, "F" a
    float, "W" 64 bits; None if it can't be said. As m2c prints the type (Type._to_ctype): an int of unknown sign prints as
    signed, an unknown register as M2C_UNK, which is s32.

    declared: the type of a variable (a register's local, a parameter or a result), which lift.py's to_register_width widens
    from u8 or u16 to u32 -- or keeps narrow, or makes s32 -- so a narrow unsigned one could be either (None). A narrow value
    that isn't a variable (a read of memory or of a stack slot, a cast) is promoted to int in C++: "S"."""
    data = t.data()
    if data.kind == TypeData.K_PTR or data.kind == TypeData.K_ARRAY:
        return "P"
    if (data.kind & TypeData.K_ANYREG) == TypeData.K_ANYREG and (
        data.likely_kind & (TypeData.K_INT | TypeData.K_FLOAT)
    ) not in (TypeData.K_INT, TypeData.K_FLOAT):
        return "S"
    if data.kind == TypeData.K_FLOAT or (data.likely_kind & (TypeData.K_FLOAT | TypeData.K_INT)) == TypeData.K_FLOAT:
        return "F"
    if data.kind != TypeData.K_INT or data.enum is not None:
        return None
    size = data.size_bits or 32
    signed = bool(data.sign & TypeData.SIGNED)
    if size == 32:
        return "S" if signed else "U"
    if size in (8, 16):
        return "S" if signed or not declared else None
    return "W" if size == 64 else None


def c_sign(expr: Expression, fmt: Formatter) -> Optional[str]:
    """How C++ compares this expression as it will be written (see c_sign_of_type), by C++'s own rules for its operators;
    None where that isn't sure. Only used to decide whether a compare needs a cast, so a None costs at most a cast that
    changes nothing."""
    e = expr
    if isinstance(e, EvalOnceExpr):
        if e.uses_var():
            return c_sign_of_type(e.var.type, True)
        return c_sign(e.wrapped_expr, fmt)
    if isinstance(e, PlannedPhiExpr):
        return c_sign_of_type(e.var.type, True)
    if isinstance(e, NaivePhiExpr):
        if e.replacement_expr is not None:
            return c_sign(e.replacement_expr, fmt)
        return c_sign_of_type(e.type, True)
    if isinstance(e, (PassedInArg, FuncCall, GlobalSymbol)):
        return c_sign_of_type(e.type, True)
    if isinstance(e, (LocalVar, ArrayAccess)):
        return c_sign_of_type(e.type, False)
    if isinstance(e, StructAccess):
        shown = e.retyped(late_unwrap(e.struct_var), fmt)
        return c_sign_of_type(shown if shown is not None else e.type, False)
    if isinstance(e, AddressOf):
        return "P"
    if isinstance(e, Literal):
        text = e.format(fmt)
        if text.endswith("U"):
            return "U"
        if text.endswith("f") or "." in text:
            return "F"
        try:
            value = int(text, 0)
        except ValueError:
            return None
        if -(1 << 31) <= value < (1 << 31):
            return "S"
        # (Past int's range, a hex or octal number is unsigned int up to 0xFFFFFFFF; a decimal one is 64 bits.)
        digits = text.lstrip("-")
        return "U" if value < (1 << 32) and digits[:1] == "0" and len(digits) > 1 else "W"
    if isinstance(e, Cast):
        if e.format(fmt) == e.expr.format(fmt):
            return c_sign(e.expr, fmt)
        return c_sign_of_type(e.type, False)
    if isinstance(e, BinaryOp):
        if e.is_comparison() or e.op in ("&&", "||"):
            return "S"
        if e.op in ("<<", ">>"):
            return c_sign(e.left, fmt)
        if e.op in ("+", "-", "*", "/", "%", "&", "|", "^"):
            return c_sign_join(c_sign(e.left, fmt), c_sign(e.right, fmt))
        return None
    if isinstance(e, UnaryOp):
        if e.op == "!":
            return "S"
        if e.op in ("-", "~"):
            return c_sign(e.expr, fmt)
        return None
    if isinstance(e, TernaryOp):
        return c_sign_join(c_sign(e.left, fmt), c_sign(e.right, fmt))
    return None


def c_sign_join(a: Optional[str], b: Optional[str]) -> Optional[str]:
    """C++'s usual arithmetic conversions on two 32-bit integers: unsigned if either is."""
    if a not in ("S", "U") or b not in ("S", "U"):
        return None
    return "U" if "U" in (a, b) else "S"


def format_compare(
    cmp: str, left: Expression, lhs: str, right: Expression, rhs: str, fmt: Formatter
) -> Tuple[str, str]:
    """No Mercy Arcade, --compare-types (2026-11-07): a compare's two sides, written so C++ compares them in the signedness of
    the instruction it came from (cmp, "S" or "U").

    m2c makes slt's compare signed by its own types, but C++ decides by the types the C++ ends up with, and those can
    differ: a u8 or u16 local becomes u32 in lift.py's to_register_width, where C++ would have promoted it to int, so
    func_8000CEEC's `slt $v0, $a0, $v0` (v0 = s2 & 0xFF) came out as `temp_a0_2 < (var_s2 & 0xFF)` with var_s2 u32 --
    unsigned, and a negative temp_a0_2 compared as a large number. And m2c can leave an expression's type as "some int" where
    C++ makes it unsigned (u32 & int). So each side is looked at the way C++ will see it (c_sign), and one that might compare
    the other way is cast: (s32), for a signed compare, on each side that isn't surely int; (u32), for an unsigned one, on a
    side when neither is surely unsigned (one unsigned side makes C++ compare both so). A cast to the register's own width
    and sign never changes the value compared: it is the 32 bits the N64 compares, read the way the instruction reads them.
    A game pointer (P<>, lift_mem.h) compares unsigned, so in a signed compare it is cast to s32 too (its explicit
    conversion gives the address): func_800EE084's `slt` loop `(s32) var_a1_2 < (arg0 + 8)` compared unsigned, and with
    arg0 = 0x7FFFFFF8 wrote four halfwords where the N64 writes one. Floats and 64-bit values are left as they are (these
    instructions compare neither)."""
    ls, rs = c_sign(left, fmt), c_sign(right, fmt)
    if {ls, rs} & {"F", "W"}:
        return lhs, rhs
    if cmp == "S":
        if ls != "S":
            lhs = f"(s32) {lhs}"
        if rs != "S":
            rhs = f"(s32) {rhs}"
        return lhs, rhs
    if "U" in (ls, rs) or "P" in (ls, rs):
        return lhs, rhs
    if isinstance(late_unwrap(left), Literal):
        return lhs, f"(u32) {rhs}"
    return f"(u32) {lhs}", rhs


@dataclass(frozen=True, eq=False)
class TernaryOp(Expression):
    cond: Condition
    left: Expression
    right: Expression
    type: Type

    def dependencies(self) -> List[Expression]:
        return [self.cond, self.left, self.right]

    def format(self, fmt: Formatter) -> str:
        cond_str = simplify_condition(self.cond).format(fmt)
        left_str = self.left.format(fmt)
        right_str = self.right.format(fmt)
        return f"({cond_str} ? {left_str} : {right_str})"


@dataclass(frozen=True, eq=False)
class UnaryOp(Condition):
    op: str
    expr: Expression
    type: Type

    def dependencies(self) -> List[Expression]:
        return [self.expr]

    @staticmethod
    def int(op: str, expr: Expression) -> UnaryOp:
        return UnaryOp(op=op, expr=as_intish(expr), type=expr.type)

    @staticmethod
    def sint(op: str, expr: Expression) -> UnaryOp:
        expr = as_sintish(expr, silent=True)
        return UnaryOp(op=op, expr=expr, type=expr.type)

    def negated(self) -> Condition:
        if self.op == "!" and isinstance(self.expr, (UnaryOp, BinaryOp)):
            return self.expr
        return UnaryOp("!", self, type=Type.boolean())

    def format(self, fmt: Formatter) -> str:
        # These aren't real operators (or functions); format them as a fn call
        if self.op in PSEUDO_FUNCTION_OPS:
            return f"{self.op}({format_expr(self.expr, fmt)})"

        return f"{self.op}{self.expr.format(fmt)}"


@dataclass(frozen=True, eq=False)
class ExprCondition(Condition):
    expr: Expression
    type: Type
    is_negated: bool = False

    def dependencies(self) -> List[Expression]:
        return [self.expr]

    def negated(self) -> Condition:
        return ExprCondition(self.expr, self.type, not self.is_negated)

    def format(self, fmt: Formatter) -> str:
        neg = "!" if self.is_negated else ""
        return f"{neg}{self.expr.format(fmt)}"


@dataclass(frozen=True, eq=False)
class CommaConditionExpr(Condition):
    statements: List[Statement]
    condition: Condition
    type: Type = Type.boolean()

    def dependencies(self) -> List[Expression]:
        assert False, "CommaConditionExpr should not be used within translate.py"
        return []

    def negated(self) -> Condition:
        return CommaConditionExpr(self.statements, self.condition.negated())

    def format(self, fmt: Formatter) -> str:
        comma_joined = ", ".join(
            stmt.format(fmt).rstrip(";") for stmt in self.statements
        )
        return f"({comma_joined}, {self.condition.format(fmt)})"


@dataclass(frozen=True, eq=True)
class Cast(Expression):
    expr: Expression
    type: Type = field(compare=False)
    reinterpret: bool = False
    silent: bool = True
    # (No Arcade, --register-cuts: a cast that is an instruction's own cut, `(s16) x` from `sll; sra`. It never changes the type of
    #  the value cut, which stays as wide as its register: unifying them made the cut vanish -- see evaluate.handle_shift_right.)
    keep_types: bool = False
    # (No Mercy Arcade, 2026-11-08: "S" or "U" for an integer-to-float conversion made from an instruction that reads its word
    #  signed (cvt.s.w, cvt.d.w) or unsigned (m2c's cvt.s.u.fictive). See format_int_to_float.)
    int_sign: Optional[str] = field(default=None, compare=False)
    # (No Mercy Arcade, 2026-11-11, scratch/different_overnight M7: "f32" or "f64" for a conversion made from an instruction that
    #  reads its register as a float -- cvt.d.s, cvt.s.d, trunc.w.s ... See Cast.format.)
    float_source: Optional[str] = field(default=None, compare=False)

    def dependencies(self) -> List[Expression]:
        return [self.expr]

    def use(self) -> None:
        # Try to unify, to make stringification output better.
        if not self.keep_types:
            self.expr.type.unify(self.type)
        super().use()

    def needed_for_store(self) -> bool:
        if not self.reinterpret:
            # int <-> float casts should be emitted even for stores.
            return True
        if not self.expr.type.unify(self.type):
            # Emit casts when types fail to unify.
            return True
        return False

    def should_wrap_transparently(self) -> bool:
        return (
            self.reinterpret
            and self.expr.type.is_float() == self.type.is_float()
            and should_wrap_transparently(self.expr)
        )

    def format(self, fmt: Formatter) -> str:
        if self.reinterpret and self.expr.type.is_float() != self.type.is_float():
            # This shouldn't happen, but mark it in the output if it does.
            if fmt.valid_syntax:
                return f"M2C_BITWISE({self.type.format(fmt)}, {self.expr.format(fmt)})"
            return f"(bitwise {self.type.format(fmt)}) {self.expr.format(fmt)}"
        if self.reinterpret and (
            self.silent
            or (is_type_obvious(self.expr) and self.expr.type.unify(self.type))
        ):
            return self.expr.format(fmt)
        if fmt.skip_casts:
            return self.expr.format(fmt)

        # Function casts require special logic because function calls have
        # higher precedence than casts
        fn_sig = self.type.get_function_pointer_signature()
        if fn_sig:
            return f"(({self.type.format(fmt)}) {self.expr.format(fmt)})"

        if COMPARE_TYPES and self.int_sign is not None:
            return f"({self.type.format(fmt)}) {format_int_to_float(self.int_sign, self.expr, fmt)}"
        if N64_CONVERSION and self.float_source is not None and not self.type.is_float():
            # (--n64-conversion: N64_CONVERSION, below. trunc.w.s / trunc.w.d / cvt.w.*: the float's bits as they are in the
            #  register when m2c's value there is an integer, as the --compare-types case just below does.)
            operand = self.expr.format(fmt)
            if not self.expr.type.is_float():
                operand = f"__builtin_bit_cast({self.float_source}, lift_plain({operand}))"
            if self.type.format(fmt) != "s32":
                # (m2c's cvt.u.s.fictive / cvt.u.d.fictive, a float to an UNSIGNED word: no candidate has one today, and
                #  C++'s (u32) of a float is another thing again on ARM64. lift.py refuses a function with M2C_ERROR.)
                return "M2C_ERROR(/* a float converted to an unsigned word: no N64 helper for it yet */)"
            return f"{'LIFT_TRUNC_W_D' if self.float_source == 'f64' else 'LIFT_TRUNC_W_S'}({operand})"
        if COMPARE_TYPES and self.float_source is not None and not self.expr.type.is_float():
            # (No Mercy Arcade, 2026-11-11, scratch/different_overnight M7) The other way round from format_int_to_float: the
            # instruction reads its register as a float, but m2c's value there is an integer -- func_80141880's `lwc1 $f0,
            # 0xA8($s0)` of a word its code also stores with sw came out as an s32 temp_f0, and its cvt.d.s as `(f64) temp_f0`,
            # which converts the integer's number (memory@801EA5C8: 200.0f stored where the original keeps 0x0B6CAB02, 389 of
            # 248,145 tests in 0.75.0). The bits are taken as they are, as the float they are in the register.
            return f"({self.type.format(fmt)}) __builtin_bit_cast({self.float_source}, lift_plain({self.expr.format(fmt)}))"
        return f"({self.type.format(fmt)}) {self.expr.format(fmt)}"


def format_int_to_float(sign: str, expr: Expression, fmt: Formatter) -> str:
    """No Mercy Arcade, --compare-types (2026-11-08): the integer an int-to-float conversion converts, written so C++ converts it
    in the signedness of the instruction it came from, as format_compare does for compares. cvt.s.w reads its word as signed, but
    C++ converts by the operand's C++ type, which can be unsigned: func_8002A0E4's `lbu $v1; ... or $v1, $v1, -0x100` (a byte
    sign-extended by hand) then `cvt.s.w` came out as `(f32) var_v1` with var_v1 a u8 local -- u32 after lift.py's
    to_register_width -- so -1 converted as 4294967295.0. A side that isn't surely the right sign (c_sign) gets (s32) or (u32):
    the register's 32 bits, read the way the instruction reads them."""
    text = expr.format(fmt)
    # (A float-typed operand, 2026-11-09: `lwc1 $f12, 0x18($s2); cvt.s.w $f12, $f12` -- func_800E67F0 reads a whole number into a
    #  float register, and the instruction converts those bits. The C++ variable is f32, and `(s32) x` would convert its value
    #  instead -- so the bits are taken as they are.)
    if expr.type.is_float():
        return f"__builtin_bit_cast({'s32' if sign == 'S' else 'u32'}, lift_plain({text}))"
    if c_sign(expr, fmt) == sign:
        return text
    return f"({'s32' if sign == 'S' else 'u32'}) {text}"


@dataclass(frozen=True, eq=False)
class FuncCall(Expression):
    function: Expression
    args: List[Expression]
    type: Type

    def dependencies(self) -> List[Expression]:
        return self.args + [self.function]

    def format(self, fmt: Formatter) -> str:
        target = late_unwrap(self.function)
        fn_sig = target.type.get_function_pointer_signature()
        if fn_sig and isinstance(target, Cast) and target.reinterpret:
            prototype_sig = target.expr.type.get_function_pointer_signature()
            if prototype_sig and prototype_sig.unify_with_args(fn_sig):
                # We only have to cast the return value of the call, not the
                # whole function pointer.
                return Cast(
                    FuncCall(target.expr, self.args, prototype_sig.return_type),
                    self.type,
                    reinterpret=True,
                    silent=True,
                ).format(fmt)

        # TODO: The function type may have a different number of params than it had
        # when the FuncCall was created. Should we warn that there may be the wrong
        # number of arguments at this callsite?
        args = ", ".join(format_expr(arg, fmt) for arg in self.args)
        return f"{target.format(fmt)}({args})"


@dataclass(frozen=True, eq=True)
class LocalVar(Expression):
    value: int
    type: Type = field(compare=False)
    path: Optional[AccessPath] = field(compare=False)

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter) -> str:
        fallback_name = f"unksp{format_hex(self.value)}"
        if self.path is None:
            return fallback_name

        name = StructAccess.access_path_to_field_name(self.path, fmt)
        if name.startswith("->"):
            return name[2:]
        return fallback_name

    def toplevel_decl(self, fmt: Formatter) -> Optional[str]:
        """Return a declaration for this LocalVar, if required."""
        # If len(self.path) > 2, then this local is an inner field of another
        # local, so it doesn't need to be declared.
        if (
            self.path is None
            or len(self.path) != 2
            or not isinstance(self.path[1], str)
        ):
            return None
        return self.type.to_decl(self.path[1], fmt)


@dataclass(frozen=True, eq=False)
class PlannedPhiExpr(Expression):
    var: Var
    type: Type
    sources: List[Reference]

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter) -> str:
        return self.var.format(fmt)


@dataclass(frozen=True, eq=True)
class PassedInArg(Expression):
    loc: ArgLoc
    stack_info: StackInfo = field(compare=False, repr=False)
    type: Type = field(compare=False)

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter) -> str:
        name = self.stack_info.get_param_name(self.loc)
        if name:
            return name
        return self.stack_info.global_info.arch.arg_name(self.loc)


@dataclass(frozen=True, eq=True)
class SubroutineArg(Expression):
    value: int
    type: Type = field(compare=False)

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter) -> str:
        return f"subroutine_arg{format_hex(self.value // 4)}"


# (No Mercy Arcade) The stores m2c writes to a stack argument's slot, FrameArgSlot, and the C++ type each writes: a word,
# a float's bits, and (2026-11-12, agent "refused") a halfword or byte -- the low bits of the register, as sh and sb store
# them. func_80122FBC's eight sh to its outgoing area came out as arguments marked "extra?" and were refused.
STACK_ARG_CTYPES: Dict[str, str] = {"sw": "s32", "swc1": "f32", "sh": "s16", "sb": "s8"}


@dataclass(frozen=True, eq=True)
class FrameArgSlot(Expression):
    """No Mercy Arcade, 2026-11-07: a stack argument's slot in the caller's frame, as memory: M2C_STACK_ARG(type, offset).

    m2c only matches a stack argument with a call in the same block. One stored in an earlier block (once before a loop, or
    before an if) was dropped, and the call got an error. Now such a store is written to the slot, and a call that finds no
    stack argument in its block reads the slot, which is what the callee reads too. The C++ keeps the frame in game memory at
    the original's offsets (LiftFrame), so the slot is the same bytes."""

    value: int
    type: Type = field(compare=False)
    ctype: str = field(compare=False, default="s32")

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter) -> str:
        return f"M2C_STACK_ARG({self.ctype}, 0x{self.value:X})"


@dataclass(frozen=True, eq=True)
class LeftInRegister(Expression):
    """No Mercy Arcade, --left-registers (2026-11-07): a parameter register passed on as the calls before left it,
    M2C_LEFT(a3). Only ever a call's argument.

    The functions given to m2c are declared with every parameter register they read (lift.py prototype), but a caller often
    sets fewer: sprintf (func_80036AD0) stores a2 and a3 for its list of extra arguments whether or not the format uses them,
    and func_80132430 reads a1 only on a path where a0 says so. What the original passes there is whatever the register held:
    for these, what the calls before this one left in it, since m2c clears the parameter registers at each call. m2c didn't
    know that value, so it wrote an error, "Read from unset register $a3" -- the commonest of its warnings in the pipeline (55
    of the 71 functions refused for one).

    The converted C++ calls through the N64's registers (lift_call: each argument put in its register, then the translated
    function run on them), and it only ever changes a parameter register by passing an argument in it. So where every way
    to this call has the register last changed by a call that was given it as an argument, by a call that runs on what was
    there before (it doesn't take it: it may change it, the same way in both), or not at all since the function was entered,
    the register already holds in the C++ exactly what the original passes. That is checked (NodeState._left_by_calls) before
    one of these is written; otherwise the error stays. lift.py then leaves the register as it is: it passes nothing in it."""

    reg: Register
    type: Type = field(compare=False)

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter) -> str:
        return f"M2C_LEFT({self.reg.register_name})"


def prints_as_pointer_to(var: Expression, target: Type, fmt: Formatter) -> bool:
    """No Mercy Arcade, 2026-11-07: whether `*var`, as printed, reads a `target` -- that is, whether var prints as a name
    (or a cast, or a number) whose type in the C is a pointer to that type.

    m2c writes a load or store at offset 0 as `*var` and leaves the type to var's. But its types are worked out by
    unification, and the C doesn't always show them: `*(temp_v1 + temp_s0)` adds two ints, `*var_s1` reads through a
    variable declared s32, `**(s32 *)0x8016CDB8` reads through the s32 the first load gave. That isn't valid C (and was
    146 of the functions refused for not compiling). The access's own type, self.type, is what the instruction reads.
    So anything that doesn't print as a pointer to it is written M2C_FIELD(var, type *, 0) instead: the value of var as
    an address, and the instruction's type there. A number is left as it was (`*0x80012345`): lift.py's
    restore_lost_sizes gives those the size their instruction has."""
    uw = late_unwrap(var)
    if isinstance(uw, Literal):
        return True
    declared: Optional[Type] = None
    if isinstance(uw, EvalOnceExpr):
        declared = uw.var.type
    elif isinstance(uw, PlannedPhiExpr):
        declared = uw.var.type
    elif isinstance(uw, (PassedInArg, LocalVar, NaivePhiExpr)):
        declared = uw.type
    elif isinstance(uw, Cast):
        declared = uw.type
    else:
        return False
    text = var.format(fmt)
    if isinstance(uw, Cast):
        # (A cast prints as one only when it isn't a silent reinterpretation: then its type is what the C sees.)
        if not text.startswith("(" + uw.type.format(fmt) + ")"):
            return False
    elif not re.fullmatch(r"[A-Za-z_]\w*", text):
        return False
    pointee = declared.get_pointer_target() if declared.is_pointer() else None
    return pointee is not None and pointee.format(fmt) == target.format(fmt)


@dataclass(eq=True, unsafe_hash=True)
class StructAccess(Expression):
    """
    Represents struct_var->offset.

    This has `eq=True` since it represents a live expression and not an
    access at a certain point in time -- this sometimes helps get rid of phi
    nodes (see `assign_naive_phis` for details). `prevent_later_uses` makes
    sure it's not used after writes/function calls that may invalidate it.
    """

    struct_var: Expression
    offset: int
    target_size: Optional[int]
    field_path: Optional[AccessPath] = field(compare=False)
    stack_info: Optional[StackInfo] = field(compare=False, repr=False)
    type: Type = field(compare=False)
    checked_late_field_path: bool = field(default=False, compare=False)
    # (No Arcade, --access-types: the size and sign of the instruction that makes this access, lb's s8, lw's s32, and whether it
    #  writes. m2c gives every access to one place a single type, so a byte read with lbu somewhere and lb elsewhere, or a word
    #  read with lw where lbu reads its first byte, came out at the wrong size or sign. When this one's differs: see format.)
    access_type: Optional[Type] = field(default=None, compare=False)
    access_store: bool = field(default=False, compare=False)

    def __post_init__(self) -> None:
        # stack_info is used to resolve field_path late
        assert (
            self.stack_info is not None or self.field_path is not None
        ), "Must provide at least one of (stack_info, field_path)"
        self.assert_valid_field_path(self.field_path)

    @staticmethod
    def assert_valid_field_path(path: Optional[AccessPath]) -> None:
        assert path is None or (
            path and isinstance(path[0], int)
        ), "The first element of the field path, if present, must be an int"

    @classmethod
    def access_path_to_field_name(cls, path: AccessPath, fmt: Formatter) -> str:
        """
        Convert an access path into a dereferencing field name, like the following examples:
            - `[0, "foo", 3, "bar"]` into `"->foo[3].bar"`
            - `[0, 3, "bar"]` into `"[0][3].bar"`
            - `[0, 1, 2]` into `"[0][1][2]"
            - `[0]` into `"[0]"`
        The path must have at least one element, and the first element must be an int.
        """
        cls.assert_valid_field_path(path)
        output = ""

        # Replace an initial "[0]." with "->"
        if len(path) >= 2 and path[0] == 0 and isinstance(path[1], str):
            output += f"->{path[1]}"
            path = path[2:]

        for p in path:
            if isinstance(p, str):
                output += f".{p}"
            elif isinstance(p, int):
                output += f"[{fmt.format_int(p)}]"
            else:
                assert_never(p)
        return output

    def dependencies(self) -> List[Expression]:
        return [self.struct_var]

    def make_reference(self) -> Optional[StructAccess]:
        field_path = self.late_field_path()
        if field_path and len(field_path) >= 2 and field_path[-1] == 0:
            return replace(self, field_path=field_path[:-1])
        return None

    def late_field_path(self) -> Optional[AccessPath]:
        # If we didn't have a type at the time when the struct access was
        # constructed, but now we do, compute field name.

        if self.field_path is None and not self.checked_late_field_path:
            var = late_unwrap(self.struct_var)
            # Format var to recursively resolve any late_field_path it has to
            # potentially improve var.type before we look up our field name
            var.format(Formatter())
            field_path, field_type, _ = var.type.get_deref_field(
                self.offset, target_size=self.target_size
            )
            if field_path is not None:
                self.assert_valid_field_path(field_path)
                self.field_path = field_path
                self.type.unify(field_type)

            self.checked_late_field_path = True
        return self.field_path

    def late_has_known_type(self) -> bool:
        if self.late_field_path() is not None:
            return True
        assert (
            self.stack_info is not None
        ), "StructAccess must have stack_info if field_path isn't set"
        if self.offset == 0:
            var = late_unwrap(self.struct_var)
            if (
                not self.stack_info.has_nonzero_access(var)
                and isinstance(var, AddressOf)
                and isinstance(var.expr, GlobalSymbol)
                and var.expr.type_provided
            ):
                return True
        return False

    def format(self, fmt: Formatter) -> str:
        var = late_unwrap(self.struct_var)
        shown = self.retyped(var, fmt)
        if shown is not None:
            pointer = Type.ptr(shown).format(fmt)
            if isinstance(var, Literal) and self.offset == 0:
                return f"*({pointer})0x{var.value & 0xFFFFFFFF:X}"
            return f"M2C_FIELD({var.format(fmt)}, {pointer}, {fmt.format_int(self.offset)})"
        has_nonzero_access = False
        if self.stack_info is not None:
            has_nonzero_access = self.stack_info.has_nonzero_access(var)

        field_path: Optional[AccessPath] = self.late_field_path()

        if field_path is not None and field_path != [0]:
            has_nonzero_access = True
        elif fmt.valid_syntax and (self.offset != 0 or has_nonzero_access):
            offset_str = fmt.format_int(self.offset)
            return f"M2C_FIELD({var.format(fmt)}, {Type.ptr(self.type).format(fmt)}, {offset_str})"
        else:
            prefix = "unk" + ("_" if fmt.coding_style.unknown_underscore else "")
            field_path = [0, prefix + format_hex(self.offset)]
        field_name = self.access_path_to_field_name(field_path, fmt)

        # Rewrite `(&x)->y` to `x.y` by stripping `AddressOf` & setting deref=False
        deref = True
        if (
            isinstance(var, AddressOf)
            and not var.expr.type.is_array()
            and not (
                isinstance(var.expr, GlobalSymbol) and var.expr.is_string_constant()
            )
            and field_name.startswith("->")
        ):
            var = var.expr
            field_name = field_name.replace("->", ".", 1)
            deref = False

        # Rewrite `x->unk0` to `*x` and `x.unk0` to `x`, unless has_nonzero_access
        if self.offset == 0 and not has_nonzero_access:
            if deref and fmt.valid_syntax and not prints_as_pointer_to(var, self.type, fmt):
                # (No Mercy Arcade, 2026-11-07: see prints_as_pointer_to.)
                return f"M2C_FIELD({var.format(fmt)}, {Type.ptr(self.type).format(fmt)}, 0)"
            return f"{'*' if deref else ''}{var.format(fmt)}"

        return f"{parenthesize_for_struct_access(var, fmt)}{field_name}"


def _structaccess_retyped(self: "StructAccess", var: Expression, fmt: Formatter) -> Optional[Type]:
    """No Mercy Arcade, --access-types: the type to write this access with, if the C would otherwise read or write it at
    another size, or a byte or halfword it reads at another sign, than its instruction (access_type). None to leave it.

    What the C would use is m2c's type for the place, or at offset 0 the type var prints as a pointer to: `*var_a0` reads u8
    through a `u8 *` where the instruction is lb (func_8003D780). The type written keeps m2c's where the size is the same --
    a word it uses as a float stays f32 (a lw that only moves a float's bits read as s32 would convert the number:
    func_8012EF14, func_80146EC0) -- with the instruction's sign for a byte or halfword it reads; otherwise the instruction's.
    Not a named symbol, which prints as itself with its own type (lift.py's lift_gp_value, gp's value, became a read at gp)."""
    own = self.access_type
    if own is None or (isinstance(var, AddressOf) and isinstance(var.expr, GlobalSymbol)):
        return None
    used = self.type
    # (2026-11-08) A store, though, writes what its source is, and the source is the instruction's type (make_store_real casts
    # it so): a float written through an int place, or the other way, would be converted by C++'s assignment, not moved.
    # func_8001BDDC's `swc1 $f0` to 0x8007D788, a word another instruction read with lw, came out as
    # `*(s32 *)0x8007D788 = (f32) (...)`: the float's value cut to a whole number. So a store of the other kind is written
    # with its instruction's type. (A load of the other kind keeps m2c's: its value is then bit-cast, M2C_BITWISE.)
    if self.access_store and used.get_size_bytes() == own.get_size_bytes() and own.is_float() != used.is_float():
        return own
    if not access_type_differs(own, used, self.access_store):
        if isinstance(var, Literal) and self.offset == 0:
            # (2026-11-08) A fixed address prints as `*(T *)0x...`, T from the address's own type, which can be void or of the
            # other kind while m2c's type for the place is right: func_8001BDDC's float stores to 0x8007D788 came out as
            # `*(void *)0x8007D788 = ...`, which lift.py's restore_lost_sizes then gave the word type of a lw elsewhere. Such an
            # access is written with the place's type when the address's doesn't say the same.
            pointee = var.type.get_pointer_target() if var.type.is_pointer() else None
            if pointee is not None and pointee.get_size_bytes() is not None and not access_type_differs(
                own, pointee, self.access_store
            ) and pointee.is_float() == own.is_float():
                return None
            # (2026-11-09: a load read as the other kind stays as m2c had it -- a lwc1'd word passed on as an int. Written as an f32 the
            #  assignment to the int variable would convert the number.)
            if not self.access_store and used.is_float() != own.is_float():
                # (2026-11-11, trial 5: unless the address's own type is void, which is no type to read at: `temp_f4 =
                #  *(void *)0x80104548` (func_800E1A54: a word one instruction reads with lw, another with lwc1) doesn't
                #  compile. "As m2c had it" is the place's type then, the same size: the word's bits as m2c goes on to use them.)
                if (pointee is None or pointee.get_size_bytes() is None) and used.get_size_bytes() == own.get_size_bytes():
                    return used
                return None
            return used if used.get_size_bytes() == own.get_size_bytes() and used.is_float() == own.is_float() else own
        if self.offset != 0 or isinstance(var, Literal):
            return None
        pointee = printed_pointee(var, fmt)
        if pointee is None or not access_type_differs(own, pointee, self.access_store):
            return None
    if used.get_size_bytes() == own.get_size_bytes() and not (own.get_size_bytes() in (1, 2) and not self.access_store):
        return used
    return own


StructAccess.retyped = _structaccess_retyped


def printed_pointee(var: Expression, fmt: Formatter) -> Optional[Type]:
    """No Mercy Arcade: the type var prints as a pointer to in the C (prints_as_pointer_to's reading of it), or None if it doesn't
    print as a pointer."""
    uw = late_unwrap(var)
    declared: Optional[Type] = None
    if isinstance(uw, (EvalOnceExpr, PlannedPhiExpr)):
        declared = uw.var.type
    elif isinstance(uw, (PassedInArg, LocalVar, NaivePhiExpr)):
        declared = uw.type
    elif isinstance(uw, Cast):
        if not var.format(fmt).startswith("(" + uw.type.format(fmt) + ")"):
            return None
        declared = uw.type
    else:
        return None
    if not isinstance(uw, Cast) and not re.fullmatch(r"[A-Za-z_]\w*", var.format(fmt)):
        return None
    return declared.get_pointer_target() if declared.is_pointer() else None


def access_type_differs(own: Type, shared: Type, store: bool = False) -> bool:
    """Whether an access's own type (its instruction's) reads or writes differently from another: another size, or for a byte
    or a halfword it reads, another sign. (A word is a word, whether int, pointer or float; a store writes the same bits
    whatever the sign.)"""
    size = own.get_size_bytes()
    if shared.get_size_bytes() != size:
        return True
    if size in (1, 2) and own.is_int() and not store:
        return shared.is_signed() != own.is_signed() or shared.is_unsigned() != own.is_unsigned()
    return False


@dataclass(frozen=True, eq=True)
class ArrayAccess(Expression):
    # Represents ptr[index]. eq=True for symmetry with StructAccess.
    ptr: Expression
    index: Expression
    type: Type = field(compare=False)

    def dependencies(self) -> List[Expression]:
        return [self.ptr, self.index]

    def format(self, fmt: Formatter) -> str:
        base = parenthesize_for_struct_access(self.ptr, fmt)
        index = format_expr(self.index, fmt)
        return f"{base}[{index}]"


@dataclass(eq=False)
class GlobalSymbol(Expression):
    c_symbol_name: str
    type: Type
    asm_data_entry: Optional[AsmDataEntry] = None
    symbol_in_context: bool = False
    type_provided: bool = False
    initializer_in_typemap: bool = False
    demangled_str: Optional[str] = None
    is_referenced: bool = False

    def dependencies(self) -> List[Expression]:
        return []

    def use(self) -> None:
        self.is_referenced = True

    def is_string_constant(self) -> bool:
        ent = self.asm_data_entry
        if not ent or len(ent.data) != 1 or not isinstance(ent.data[0], bytes):
            return False
        return ent.is_string

    def format_string_constant(self, fmt: Formatter) -> str:
        assert self.is_string_constant(), "checked by caller"
        assert self.asm_data_entry and isinstance(self.asm_data_entry.data[0], bytes)

        has_trailing_null = False
        data = self.asm_data_entry.data[0]
        while data and data[-1] == 0:
            data = data[:-1]
            has_trailing_null = True
        data = b"".join(map(escape_byte, data))

        strdata = data.decode("utf-8", "backslashreplace")
        ret = f'"{strdata}"'
        if not has_trailing_null:
            ret += " /* not null-terminated */"
        return ret

    def format(self, fmt: Formatter) -> str:
        return self.c_symbol_name

    def potential_array_dim(self, element_size: int) -> Tuple[int, int]:
        """
        Using the size of the symbol's `asm_data_entry` and a potential array element
        size, return the corresponding array dimension and number of "extra" bytes left
        at the end of the symbol's data.
        If the extra bytes are nonzero, then it's likely that `element_size` is incorrect.
        """
        assert element_size != 0, "potential_array_dim cannot be called on zero size"

        # If we don't have the .data/.rodata entry for this symbol, we can't guess
        # its array dimension. Jump tables are ignored and not treated as arrays.
        if self.asm_data_entry is None or self.asm_data_entry.is_jtbl:
            return 0, element_size

        min_data_size, max_data_size = self.asm_data_entry.size_range_bytes()
        if element_size > max_data_size:
            # The type is too big for the data (not an array)
            return 0, max_data_size

        # Check if it's possible that this symbol is not an array, and is just 1 element
        if min_data_size <= element_size <= max_data_size and not self.type.is_array():
            return 1, 0

        array_dim, extra_bytes = divmod(min_data_size, element_size)
        if extra_bytes != 0:
            # If it's not possible to make an exact multiple of element_size by incorporating
            # bytes from the padding, then indicate that in the return value.
            padding_bytes = element_size - extra_bytes
            if min_data_size + padding_bytes > max_data_size:
                return array_dim, extra_bytes

        # Include potential padding in the array. Although this is unlikely to match the original C,
        # it's much easier to manually remove all or some of these elements than to add them back in.
        return max_data_size // element_size, 0


@dataclass(frozen=True, eq=True)
class Literal(Expression):
    value: int
    type: Type = field(compare=False, default_factory=Type.any)
    elide_cast: bool = field(compare=False, default=False)

    def dependencies(self) -> List[Expression]:
        return []

    def format(self, fmt: Formatter, force_dec: bool = False) -> str:
        enum_name = self.type.get_enum_name(self.value)
        if enum_name is not None:
            return enum_name

        if self.type.is_likely_float():
            if self.type.get_size_bits() == 64:
                return format_f64_imm(self.value)
            else:
                return format_f32_imm(self.value) + "f"
        if self.type.is_pointer() and self.value == 0:
            return "NULL"

        prefix = ""
        suffix = ""
        if not fmt.skip_casts and not self.elide_cast:
            if self.type.is_pointer():
                prefix = f"({self.type.format(fmt)})"
            if self.type.is_unsigned():
                suffix = "U"

        if force_dec:
            value = str(self.value)
        else:
            size_bits = self.type.get_size_bits()
            v = self.value

            # The top 2 bits are tested rather than just the sign bit
            # to help prevent N64 VRAM pointers (0x80000000+) turning negative
            if (
                self.type.is_signed()
                and size_bits
                and v & (1 << (size_bits - 1))
                and v > (3 << (size_bits - 2))
                and v < 2**size_bits
            ):
                v -= 1 << size_bits
            value = fmt.format_int(v, size_bits=size_bits)

        return prefix + value + suffix


@dataclass(frozen=True, eq=True)
class AddressOf(Expression):
    expr: Expression
    type: Type = field(compare=False, default_factory=Type.ptr)

    def dependencies(self) -> List[Expression]:
        return [self.expr]

    def format(self, fmt: Formatter) -> str:
        if isinstance(self.expr, GlobalSymbol):
            if self.expr.is_string_constant():
                return self.expr.format_string_constant(fmt)
        if self.expr.type.is_array():
            return f"{self.expr.format(fmt)}"
        if self.expr.type.is_function():
            # Functions are automatically converted to function pointers
            # without an explicit `&` by the compiler
            return f"{self.expr.format(fmt)}"
        if isinstance(self.expr, StructAccess):
            # Simplify `&x[0]` into `x`
            ref = self.expr.make_reference()
            if ref:
                return f"{ref.format(fmt)}"
        if isinstance(self.expr, SubroutineArg) and fmt.valid_syntax:
            # (No Mercy Arcade, 2026-11-11, trial 5: an address low in the frame, where the arguments for calls go, as the
            #  stack pointer plus its offset. It printed as `&subroutine_arg1`, a name nothing declares, and the name is the
            #  word's (offset / 4), so the offset's last two bits were lost: func_8010ECC4's `addu $v0, $sp, $s0` with s0 = 6,
            #  the start of an array it then reads at 0x30 on from there. `sp` is what an indexed address already prints as
            #  (`sp + var_s0`, the same instruction on another path), and lift.py gives it the frame's place.)
            return f"(sp + {fmt.format_int(self.expr.value)})"
        return f"&{self.expr.format(fmt)}"


@dataclass(frozen=True)
class Lwl(Expression):
    load_expr: Expression
    key: Tuple[int, object]
    type: Type = field(compare=False, default_factory=Type.any_reg)

    def dependencies(self) -> List[Expression]:
        return [self.load_expr]

    def format(self, fmt: Formatter) -> str:
        return f"M2C_LWL({self.load_expr.format(fmt)})"


@dataclass(frozen=True)
class Load3Bytes(Expression):
    load_expr: Expression
    type: Type = field(compare=False, default_factory=Type.any_reg)

    def dependencies(self) -> List[Expression]:
        return [self.load_expr]

    def format(self, fmt: Formatter) -> str:
        if fmt.valid_syntax:
            return f"M2C_FIRST3BYTES({self.load_expr.format(fmt)})"
        return f"(first 3 bytes) {self.load_expr.format(fmt)}"


@dataclass(frozen=True)
class UnalignedLoad(Expression):
    load_expr: Expression
    type: Type = field(compare=False, default_factory=Type.any_reg)

    def dependencies(self) -> List[Expression]:
        return [self.load_expr]

    def format(self, fmt: Formatter) -> str:
        if fmt.valid_syntax:
            return f"M2C_UNALIGNED32({self.load_expr.format(fmt)})"
        return f"(unaligned s32) {self.load_expr.format(fmt)}"


@dataclass(frozen=False, eq=False)
class EvalOnceExpr(Expression):
    wrapped_expr: Expression
    var: Var
    type: Type

    sources: List[Reference] = field(repr=False)

    # True for function calls/errors
    emit_exactly_once: bool

    # True if this EvalOnceExpr should be totally transparent and not use a variable
    # even if it gets assigned to a planned phi var. Based on `is_trivial_expression`.
    trivial: bool

    # True if this EvalOnceExpr should be totally transparent and not use a variable.
    # If `var.is_emitted` is true it is ignored (this may happen dynamically, due to
    # forced emissions caused by `prevent_later_uses`).
    # Based on `should_wrap_transparently` (except for function returns). Always true
    # if `trivial` is true.
    transparent: bool

    is_fictive_temp: bool

    # Mutable state:

    # True if this EvalOnceExpr must use a variable (see RegMeta.force)
    forced_emit: bool = False

    # True if this EvalOnceExpr has been use()d. If `var.is_emitted` is true, this will
    # also be: either because this EvalOnceExpr was used twice and that triggered
    # `var.is_emitted` to be set to true, or because the var is a planned phi, and then
    # this EvalOnceExpr will be use()d as part of its creation, for the assignment to
    # the phi.
    is_used: bool = False

    # (No Mercy Arcade, 2026-11-11, trial 5) True once something this value was made from has changed since it was made: a
    # variable in it assigned again, memory it read written. From then on it is only right as its own variable, assigned
    # where it was made, and nothing may look inside it for the parts (early_unwrap). See mark_stale.
    stale: bool = False

    def dependency(self, may_move_forward: bool) -> Optional[Expression]:
        # (It is a bit iffy to have this method depend on state that changes over time,
        # but it improves uses_expr. Ideally we'd know dependencies ahead of time so
        # _prevent_later_uses can do a better job.)
        if self.trivial:
            return self.wrapped_expr
        if self.var.is_emitted:
            return None
        if not self.transparent and self.is_used and not may_move_forward:
            # If the expression is already used once, any further use of it will cause
            # a var to be emitted, meaning we don't have a dependency on it at this
            # point in time -- that dependency only existed at the time of the var
            # assignment. The exception to this is if the expression is marked as
            # emit_exactly_once, or is recursively contained within an EvalOnceExpr
            # which is: in that case the use may be moved forward in time which breaks
            # this reasoning.
            return None
        return self.wrapped_expr

    def dependencies(self) -> List[Expression]:
        assert False, "never called, and it's unclear what to pass for may_move_forward"

    def use(self) -> None:
        if self.trivial or (self.transparent and not self.var.is_emitted):
            # Forward uses through transparent wrappers (unless we have stopped
            # being transparent)
            self.wrapped_expr.use()
        elif not self.is_used:
            # On first use, forward the use (except in the emit_exactly_once
            # case where that has already happened).
            if not self.emit_exactly_once:
                self.wrapped_expr.use()
        else:
            # On second use, make sure a var gets emitted.
            self.var.is_emitted = True
        self.is_used = True

    def force(self) -> None:
        # Transition to opaque, and mark as used multiple times to force a var.
        # TODO: If it was originally transparent, we may previously have marked
        # its wrappee used multiple times, even though we now know that it should
        # have been marked just once... We could fix that by moving marking of
        # transparent EvalOnceExpr's to the very end. At least the consequences of
        # getting this wrong are pretty mild -- it just causes extraneous var
        # emission in rare cases.
        if not self.forced_emit and not self.trivial:
            self.forced_emit = True
            self.var.is_emitted = True
            self.use()
            self.use()

    def uses_var(self) -> bool:
        return self.var.is_emitted and not self.trivial

    def format(self, fmt: Formatter) -> str:
        if not self.uses_var():
            return self.wrapped_expr.format(fmt)
        else:
            return self.var.format(fmt)


@dataclass(frozen=False, eq=False)
class NaivePhiExpr(Expression):
    """
    A NaivePhiExpr acts as a phi node, using terminology from [1], with one
    of two possible behaviors:
    - if `replacement_expr` is set, it forwards forwarding/use to that. This
      happens when all phi input values are the same.
    - otherwise, it expands to a "phi_x" variable, with assignments
      (SetNaivePhiStmt) being added to the end of the basic blocks that the
      phi gets assigned from.

    The second behavior results in unideal output in a couple of ways:
    - code bloat caused by multiple phi nodes with overlapping sources each
      needing separate sets of assignments,
    - code bloat caused by temp and phi assignments being separate,
    - incorrect codegen caused by end-of-block phi assignments happening one
      by one instead of in parallel (consider e.g. wanting to perform the
      end-of-block phi updates "(phi_a1, phi_a2) = (phi_a2, phi_a1)").

    Thus, we try our hardest to avoid these kinds of NaivePhiExpr's by having
    `assign_naive_phis` signal to future translation passes to use the
    alternative phi node kind, PlannedPhiExpr, that's based on pre-planned
    assignments to temps. (Unfortunately we can't use PlannedPhiExpr during the
    first pass, because we don't know ahead of time which NaivePhiExpr's
    will end up using a replacement_expr, or for that sake, even which
    registers will end up being read as phis -- function pointers makes this a
    dynamic property dependent on type info. Both problems are potentially
    resolvable but it would involve a fair bit of work.)

    A NaivePhiExpr that isn't use()'d is treated as if it doesn't exist, and in
    particular does not add assignment statements to its source nodes. On first
    use() it adds itself to the global `used_naive_phis` queue, which is
    processed by `assign_naive_phis`.

    [1] https://en.wikipedia.org/wiki/Static_single_assignment_form
    """

    reg: Register
    node: Node
    type: Type
    sources: List[Reference]
    uses_dominator: bool
    used_naive_phis: List[NaivePhiExpr]  # reference to global shared list
    name: Optional[str] = None
    num_usages: int = 0
    replacement_expr: Optional[Expression] = None

    def dependencies(self) -> List[Expression]:
        return []

    def get_var_name(self) -> str:
        return self.name or f"unnamed-phi({self.reg.register_name})"

    def use(self) -> None:
        if self.num_usages == 0:
            self.used_naive_phis.append(self)
        self.num_usages += 1
        if self.replacement_expr is not None:
            self.replacement_expr.use()

    def format(self, fmt: Formatter) -> str:
        if self.replacement_expr is not None:
            return self.replacement_expr.format(fmt)
        return self.get_var_name()


@dataclass
class SwitchControl:
    control_expr: Expression
    num_cases: Optional[int] = None
    jump_table: Optional[GlobalSymbol] = None
    offset: int = 0
    is_irregular: bool = False

    def matches_guard_condition(self, cond: Condition) -> bool:
        """
        Return True if `cond` is one of:
            - `((control_expr + (-offset)) >= len(jump_table))`, if `offset != 0`
            - `(control_expr >= len(jump_table))`, if `offset == 0`
        These are the appropriate bounds checks before using `jump_table`.
        """
        cmp_expr = simplify_condition(cond)
        if not isinstance(cmp_expr, BinaryOp) or cmp_expr.op not in (">=", ">"):
            return False
        cmp_exclusive = cmp_expr.op == ">"

        # The LHS may have been wrapped in a u32 cast
        left_expr = late_unwrap_ints(cmp_expr.left)

        if self.offset != 0:
            if (
                not isinstance(left_expr, BinaryOp)
                or late_unwrap(left_expr.left) != late_unwrap(self.control_expr)
                or left_expr.op != "+"
                or late_unwrap(left_expr.right) != Literal(-self.offset)
            ):
                return False
        elif left_expr != late_unwrap_ints(self.control_expr):
            return False

        right_expr = late_unwrap(cmp_expr.right)
        if not isinstance(right_expr, Literal):
            return False

        return right_expr.value + int(cmp_exclusive) == self.num_cases

    @staticmethod
    def irregular_from_expr(control_expr: Expression) -> SwitchControl:
        """
        Return a SwitchControl representing a "irregular" switch statement.
        The switch does not have a single jump table; instead it is a series of
        if statements & other switches.
        """
        return SwitchControl(control_expr=control_expr, offset=0, is_irregular=True)

    @staticmethod
    def from_expr(expr: Expression, num_cases: int) -> SwitchControl:
        """
        Try to convert `expr` into a SwitchControl from one of the following forms:
            - `*(&jump_table + (control_expr * 4))`
            - `*(&jump_table + ((control_expr + (-offset)) * 4))`
        If `offset` is not present, it defaults to 0.
        If `expr` does not match, return a thin wrapper around the input expression,
        with `jump_table` set to `None`.
        """
        # The "error" expression we use if we aren't able to parse `expr`
        error_expr = SwitchControl(expr)

        # Match `*(&jump_table + (control_expr * 4))`
        struct_expr = early_unwrap(expr)
        if not isinstance(struct_expr, StructAccess) or struct_expr.offset != 0:
            return error_expr
        add_expr = early_unwrap(struct_expr.struct_var)
        if not isinstance(add_expr, BinaryOp) or add_expr.op != "+":
            return error_expr

        # Check for either `*(&jump_table + (control_expr * 4))` and `*((control_expr * 4) + &jump_table)`
        left_expr, right_expr = early_unwrap(add_expr.left), early_unwrap(
            add_expr.right
        )
        if isinstance(left_expr, AddressOf) and isinstance(
            left_expr.expr, GlobalSymbol
        ):
            jtbl_addr_expr, mul_expr = left_expr, right_expr
        elif isinstance(right_expr, AddressOf) and isinstance(
            right_expr.expr, GlobalSymbol
        ):
            mul_expr, jtbl_addr_expr = left_expr, right_expr
        else:
            return error_expr

        jump_table = jtbl_addr_expr.expr
        assert isinstance(jump_table, GlobalSymbol)
        if (
            not isinstance(mul_expr, BinaryOp)
            or mul_expr.op != "*"
            or early_unwrap(mul_expr.right) != Literal(4)
        ):
            return error_expr
        control_expr = mul_expr.left

        # Optionally match `control_expr + (-offset)`
        offset = 0
        uw_control_expr = early_unwrap(control_expr)
        if isinstance(uw_control_expr, BinaryOp) and uw_control_expr.op == "+":
            offset_lit = early_unwrap(uw_control_expr.right)
            if isinstance(offset_lit, Literal):
                control_expr = uw_control_expr.left
                offset = -offset_lit.value

        # Check that it is really a jump table
        # TODO: check that it's actually the same jump table as we found in the
        # flow_graph pre-check
        if jump_table.asm_data_entry is None or not jump_table.asm_data_entry.is_jtbl:
            return error_expr

        return SwitchControl(control_expr, num_cases, jump_table, offset)


@dataclass
class EvalOnceStmt(Statement):
    expr: EvalOnceExpr

    def should_write(self) -> bool:
        if self.expr.emit_exactly_once and not self.expr.is_used:
            return True
        if not self.expr.var.is_emitted:
            return False
        if var_for_expr(late_unwrap(self.expr.wrapped_expr)) == self.expr.var:
            return False
        return True

    def format(self, fmt: Formatter) -> str:
        val = elide_literal_casts(self.expr.wrapped_expr)
        if self.expr.emit_exactly_once and not self.expr.is_used:
            val_str = format_expr(val, fmt)
            return f"{val_str};"
        return format_assignment(self.expr.var, val, fmt)


@dataclass
class SetNaivePhiStmt(Statement):
    phi: NaivePhiExpr
    expr: Expression

    def should_write(self) -> bool:
        # Elide "phi = phi"
        return late_unwrap(self.expr) != self.phi

    def format(self, fmt: Formatter) -> str:
        return format_assignment(self.phi, self.expr, fmt)


@dataclass
class ExprStmt(Statement):
    expr: Expression

    def should_write(self) -> bool:
        return True

    def format(self, fmt: Formatter) -> str:
        return f"{format_expr(self.expr, fmt)};"


@dataclass
class StoreStmt(Statement):
    source: Expression
    dest: Expression

    def should_write(self) -> bool:
        return True

    def format(self, fmt: Formatter) -> str:
        dest = self.dest
        source = self.source
        if (
            isinstance(dest, StructAccess) and dest.late_has_known_type()
        ) or isinstance(dest, (ArrayAccess, LocalVar, SubroutineArg)):
            # Known destination; fine to elide some casts.
            source = elide_literal_casts(source)
        # (No Mercy Arcade, 2026-11-08) A number stored as the other kind than the place it's written to prints as that place's
        # kind with the same bits: C++'s assignment would convert it. func_8013AB54 makes 1.0f with `lui $at, 0x3F80; mtc1
        # $at, $f2` and stores it with swc1 into a float field; m2c kept the number's int type and wrote
        # `M2C_FIELD(.., f32 *, 0xF8) = 0x3F800000;`, which stores 1065353216.0f. (The place's type as the access is written:
        # StructAccess.retyped.)
        number = late_unwrap(source)
        if isinstance(number, Literal) and isinstance(dest, StructAccess):
            shown = dest.retyped(late_unwrap(dest.struct_var), fmt) or dest.type
            size = shown.get_size_bits()
            if size in (32, 64) and shown.is_float() != number.type.is_likely_float():
                source = Literal(number.value, type=Type.f32() if size == 32 and shown.is_float() else
                                 Type.f64() if shown.is_float() else Type.s32() if size == 32 else Type.s64())
        elif isinstance(dest, StructAccess):
            # (2026-11-09) And a value of the other kind, not a number: `swc1` of an integer the source casts to s32
            # (func_8002BF70: `M2C_FIELD(arg0, P<f32> , 0x44) = (s32) M2C_FIELD(..)`), which C++'s assignment would convert.
            shown = dest.retyped(late_unwrap(dest.struct_var), fmt) or dest.type
            if shown.get_size_bits() == 32 and source.type.get_size_bits() == 32:
                if shown.is_float() and source.type.is_int():
                    return f"{dest.format(fmt)} = __builtin_bit_cast(f32, lift_plain({format_expr(source, fmt)}));"
                if shown.is_int() and source.type.is_float():
                    return f"{dest.format(fmt)} = __builtin_bit_cast(s32, lift_plain({format_expr(source, fmt)}));"
        return format_assignment(dest, source, fmt)


@dataclass
class CommentStmt(Statement):
    contents: str

    def should_write(self) -> bool:
        return True

    def format(self, fmt: Formatter) -> str:
        return f"// {self.contents}"


@dataclass(frozen=True)
class AddressMode:
    offset: int
    base: Register

    def __str__(self) -> str:
        if self.offset:
            return f"{self.offset}({self.base})"
        else:
            return f"({self.base})"


@dataclass(frozen=True)
class RawSymbolRef:
    offset: int
    sym: AsmGlobalSymbol

    def __str__(self) -> str:
        if self.offset:
            return f"{self.sym.symbol_name} + {self.offset}"
        else:
            return self.sym.symbol_name


@dataclass
class RegMeta:
    """Metadata associated with a register value assignment.

    This could largely be computed ahead of time based on instr_inputs/
    instr_uses, except for the fact that function pointer argument passing
    is determined dynamically based on type info.

    Except for a few properties, metadata is propagated across blocks only
    after block translation is done, as part of propagate_register_meta,
    and only for a handful of registers. (Parent block metadata is not
    generally available when constructing reg metas, due to cycles)."""

    # True if this regdata is unchanged from the start of the block
    inherited: bool = False

    # True if this regdata is unchanged from the start of the function
    # Propagated early
    initial: bool = False

    # True if this regdata is read by some later node
    is_read: bool = False

    # True if the value derives solely from function call return values
    function_return: bool = False

    # True if the value derives solely from regdata's with is_read = True,
    # function_return = True, or is a passed in argument
    uninteresting: bool = False

    # True if the regdata must be replaced by variable if it is ever read
    # Propagated early
    force: bool = False

    # True if the regdata was assigned by an Instruction marked as in_pattern;
    # it was part of a matched IR pattern but couldn't be elided at the time
    in_pattern: bool = False


RegExpression = Union[EvalOnceExpr, PlannedPhiExpr, NaivePhiExpr]


@dataclass
class RegData:
    value: RegExpression
    meta: RegMeta


@dataclass
class RegInfo:
    stack_info: StackInfo = field(repr=False)
    contents: Dict[Register, RegData] = field(default_factory=dict)
    read_inherited: Set[Register] = field(default_factory=set)
    _current_instr_ref: Optional[InstrRef] = None

    def current_instr_ref(self) -> InstrRef:
        assert self._current_instr_ref is not None
        return self._current_instr_ref

    def current_instr(self) -> Instruction:
        return self.current_instr_ref().instruction

    def has_current_instr(self) -> bool:
        return self._current_instr_ref is not None

    @contextmanager
    def set_current_instr(self, instr_ref: InstrRef) -> Iterator[None]:
        assert self._current_instr_ref is None
        self._current_instr_ref = instr_ref
        try:
            with set_current_instr(instr_ref.instruction):
                yield
        finally:
            self._current_instr_ref = None

    def __getitem__(self, key: Register) -> Expression:
        if self.has_current_instr() and key not in self.current_instr().inputs:
            lineno = self.get_instruction_lineno()
            return ErrorExpr(f"Read from unset register {key} on line {lineno}")
        if key == Register("zero"):
            return Literal(0)
        data = self.contents.get(key)
        if data is None:
            return ErrorExpr(f"Read from unset register {key}")
        ret = data.value
        meta = data.meta
        meta.is_read = True
        if meta.inherited:
            self.read_inherited.add(key)
        if isinstance(ret, EvalOnceExpr) and ret.trivial:
            # Unwrap trivial wrappers eagerly: this helps phi equality checks, and
            # removes the need for unwrapping at each place that deals with literals.
            return ret.wrapped_expr
        if meta.initial:
            uw_ret = unwrap_deep(ret)
            if isinstance(uw_ret, PassedInArg):
                # Use accessed argument registers as a signal for determining which
                # arguments actually exist.
                arg = self.stack_info.get_argument(uw_ret.loc)
                self.stack_info.add_argument(arg)
                arg.type.unify(ret.type)
        if meta.force:
            assert isinstance(ret, EvalOnceExpr)
            ret.force()
        return ret

    def __contains__(self, key: Register) -> bool:
        return key in self.contents

    def set_with_meta(self, key: Register, value: RegExpression, meta: RegMeta) -> None:
        """Assign a value to a register from inside an instruction context."""
        instr = self.current_instr()
        if key not in instr.outputs:
            raise DecompFailure(f"Undeclared write to {key} in {instr}")
        assert key != Register("zero")
        self.contents[key] = RegData(value, meta)

    def global_set_with_meta(
        self, key: Register, value: RegExpression, meta: RegMeta
    ) -> None:
        """Assign a value to a register from outside an instruction context."""
        assert not self.has_current_instr()
        assert key != Register("zero")
        self.contents[key] = RegData(value, meta)

    def update_meta(self, key: Register, meta: RegMeta) -> None:
        assert key != Register("zero")
        self.contents[key] = RegData(self.contents[key].value, meta)

    def __delitem__(self, key: Register) -> None:
        assert key != Register("zero")
        del self.contents[key]

    def get_raw(self, key: Register) -> Optional[RegExpression]:
        data = self.contents.get(key)
        return data.value if data is not None else None

    def get_meta(self, key: Register) -> Optional[RegMeta]:
        data = self.contents.get(key)
        return data.meta if data is not None else None

    def get_instruction_lineno(self) -> int:
        if not self.has_current_instr():
            return 0
        return self.current_instr().meta.lineno

    def __str__(self) -> str:
        return ", ".join(
            f"{k}: {v.value}"
            for k, v in sorted(self.contents.items(), key=lambda x: x[0].register_name)
            if not self.stack_info.should_save(v.value, None)
        )


@dataclass
class BlockInfo:
    """
    Contains translated assembly code (to_write), the block's branch condition,
    and block's final register states.
    """

    to_write: List[Statement]
    return_value: Optional[Expression]
    switch_control: Optional[SwitchControl]
    branch_condition: Optional[Condition]
    final_register_states: RegInfo
    has_function_call: bool

    def __str__(self) -> str:
        newline = "\n\t"
        return "\n".join(
            [
                f"Statements: {newline.join(str(w) for w in self.statements_to_write())}",
                f"Branch condition: {self.branch_condition}",
                f"Final register states: {self.final_register_states}",
            ]
        )

    def statements_to_write(self) -> List[Statement]:
        return [st for st in self.to_write if st.should_write()]


def visualize_flowgraph(
    flow_graph: FlowGraph, viz_type: Options.VisualizeTypeEnum
) -> str:
    fmt = Formatter(debug=True)
    nodes: List[Tuple[str, List[str]]] = []
    edges: List[Tuple[str, str, Dict[str, str]]] = []

    def emit_node(name: str, lines: List[str]) -> None:
        nodes.append((name, lines))

    def emit_edge(source: str, target: str, **attrs: str) -> None:
        edges.append((source, target, attrs))

    for node in flow_graph.nodes:
        block_info = _get_optional_block_info(node)
        asm_lines = []
        c_lines = []

        if block_info:
            asm_lines = [
                f"{'*' if i.meta.synthetic else '&nbsp;'} {i}"
                for i in node.block.instructions
            ]
            c_lines = [w.format(fmt) for w in block_info.to_write if w.should_write()]

        if isinstance(node, BasicNode):
            emit_edge(node.name(), node.successor.name(), color="black")
        elif isinstance(node, ConditionalNode):
            if block_info:
                assert block_info.branch_condition is not None
                c_lines.append(f"if ({block_info.branch_condition.format(fmt)})")
            emit_edge(
                node.name(), node.fallthrough_edge.name(), label="F", color="blue"
            )
            emit_edge(node.name(), node.conditional_edge.name(), label="T", color="red")
        elif isinstance(node, ReturnNode):
            if block_info is not None and block_info.return_value:
                c_lines.append(f"return ({block_info.return_value.format(fmt)});")
            else:
                c_lines.append("return;")
            emit_edge(node.name(), node.terminal.name())
        elif isinstance(node, SwitchNode):
            assert block_info is not None
            switch_control = block_info.switch_control
            assert switch_control is not None
            c_lines.append(f"switch ({switch_control.control_expr.format(fmt)})")
            for i, case in enumerate(node.cases):
                emit_edge(
                    node.name(),
                    case.name(),
                    label=str(i + switch_control.offset),
                    color="green",
                )
        else:
            assert isinstance(node, TerminalNode)
            asm_lines.append("// exit")
            c_lines.append("// exit")

        line_label = ""
        first_instr = next(node.block.instructions, None)
        if first_instr is not None and first_instr.meta.lineno > 0:
            line_label = f" (line {first_instr.meta.lineno})"

        lines = [f"// Node {node.name()}{line_label}"]
        if viz_type == Options.VisualizeTypeEnum.ASM:
            lines += asm_lines
        elif viz_type == Options.VisualizeTypeEnum.C:
            lines += c_lines
        else:
            raise ValueError("Invalid viz_type: {viz_type!r}")
        emit_node(node.name(), lines)

    def escape(value: str) -> str:
        return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")

    def quote(value: str) -> str:
        return '"' + escape(value) + '"'

    lines = [
        "digraph {",
        '  node [shape="rect", fontname="Monospace"];',
        '  edge [fontname="Monospace"];',
    ]
    for name, source in nodes:
        label = "".join(escape(line) + "\\l" for line in source)
        lines.append(f'  {quote(name)} [label="{label}"];')
    for source, target, attrs in edges:
        attr_strs = [f"{name}={quote(value)}" for name, value in attrs.items()]
        attr_text = f" [{', '.join(attr_strs)}]" if attr_strs else ""
        lines.append(f"  {quote(source)} -> {quote(target)}{attr_text};")
    lines.append("}")
    return "\n".join(lines) + "\n"


def get_block_info_for_block(block: Block) -> BlockInfo:
    ret = block.block_info
    assert isinstance(ret, BlockInfo)
    return ret


def get_block_info(node: Node) -> BlockInfo:
    return get_block_info_for_block(node.block)


def _get_optional_block_info(node: Node) -> Optional[BlockInfo]:
    if node.block.block_info is None:
        return None
    return get_block_info(node)


@dataclass
class InstrArgs:
    instruction_ref: InstrRef
    raw_args: List[Argument]
    regs: RegInfo = field(repr=False)
    stack_info: StackInfo = field(repr=False)

    def raw_arg(self, index: int) -> Argument:
        assert index >= 0
        if index >= len(self.raw_args):
            raise DecompFailure(
                f"Too few arguments for instruction, expected at least {index + 1}"
            )
        return self.raw_args[index]

    def reg_ref(self, index: int) -> Register:
        ret = self.raw_arg(index)
        if not isinstance(ret, Register):
            raise DecompFailure(
                f"Expected instruction argument to be a register, but found {ret}"
            )
        return ret

    def imm_value(self, index: int) -> int:
        arg = self.full_imm(index)
        assert isinstance(arg, Literal)
        return arg.value

    def reg(self, index: int) -> Expression:
        return self.regs[self.reg_ref(index)]

    def dreg(self, index: int) -> Expression:
        """Extract a double from a register. This may involve reading both the
        mentioned register and the next."""
        reg = self.reg_ref(index)
        if (
            self.stack_info.global_info.arch.arch != Target.ArchEnum.ARM
            and not reg.is_float()
        ):
            raise DecompFailure(
                f"Expected instruction argument {reg} to be a float register"
            )
        ret = self.regs[reg]

        # PPC: FPR's hold doubles (64 bits), so we don't need to do anything special
        if self.stack_info.global_info.arch.arch == Target.ArchEnum.PPC:
            return ret

        # ARM/MIPS: Look at the paired FPR to get the full 64-bit value
        if ret.type.get_size_bits() == 64:
            return ret
        if not isinstance(ret, Literal):
            # (No Mercy Arcade, 2026-11-12, agent "refused") A double made from two 32-bit halves that aren't numbers m2c
            # knows: func_800353C0's `lw $s6, 0x6368($at); lw $s7, 0x636C($at); mtc1 $s7, $f18; mtc1 $s6, $f19; div.d $f0,
            # $f18, $f0`. Upstream m2c took the even register's 32-bit value for the whole double and dropped the odd one,
            # which with --compare-types came out as a bit-cast of 4 bytes to 8 (it did not compile). The halves are glued,
            # as the literal case below does: GLUE_F64(high, low), lift_mem.h. Only where the even register surely holds a
            # 32-bit value, and the odd one is not the second half of a double already there.
            reg_num = int(reg.register_name[1:])
            other_reg = Register(f"{reg.register_name[0]}{reg_num ^ 1}")
            if (
                not COMPARE_TYPES
                or reg_num % 2 != 0
                or ret.type.get_size_bits() != 32
                or other_reg not in self.regs
            ):
                return ret
            other = self.regs[other_reg]
            if isinstance(other, SecondF64Half) or other.type.get_size_bits() == 64:
                return ret
            return BinaryOp(op="GLUE_F64", left=other, right=ret, type=Type.f64())
        reg_num = int(reg.register_name[1:])
        if reg_num % 2 != 0:
            raise DecompFailure(
                "Tried to use a double-precision instruction with odd-numbered float "
                f"register {reg}"
            )
        other = self.regs[Register(f"{reg.register_name[0]}{reg_num+1}")]
        if isinstance(other, Literal) and other.type.get_size_bits() != 64:
            value = ret.value | (other.value << 32)
            return Literal(value, type=Type.f64())
        ret.type.unify(Type.uintish())
        other.type.unify(Type.uintish())
        return BinaryOp(op="GLUE_F64", left=other, right=ret, type=Type.f64())

    def cmp_reg(self, key: str) -> Condition:
        cond = self.regs[Register(key)]
        if not isinstance(cond, Condition):
            cond = BinaryOp.icmp(cond, "!=", Literal(0))
        return cond

    def full_imm(self, index: int) -> Expression:
        arg = strip_macros(self.raw_arg(index))
        ret = literal_expr(arg, self.stack_info)
        return ret

    def s8_imm(self, index: int) -> Expression:
        ret = self.full_imm(index)
        if isinstance(ret, Literal):
            return Literal(((ret.value + 0x80) & 0xFF) - 0x80)
        return ret

    def s16_imm(self, index: int) -> Expression:
        ret = self.full_imm(index)
        if isinstance(ret, Literal):
            return Literal(((ret.value + 0x8000) & 0xFFFF) - 0x8000)
        return ret

    def u16_imm(self, index: int) -> Expression:
        ret = self.full_imm(index)
        if isinstance(ret, Literal):
            return Literal(ret.value & 0xFFFF)
        return ret

    def reg_or_imm(self, index: int) -> Expression:
        arg = self.raw_arg(index)
        if isinstance(arg, Register):
            return self.regs[arg]
        return self.full_imm(index)

    def reg_or_s8_imm(self, index: int) -> Expression:
        arg = self.raw_arg(index)
        if isinstance(arg, Register):
            return self.regs[arg]
        return self.s8_imm(index)

    def hi_imm(self, index: int) -> RawSymbolRef:
        arg = self.raw_arg(index)
        if not isinstance(arg, Macro) or arg.macro_name not in ("hi", "ha", "h"):
            raise DecompFailure(
                f"lui/lis argument must be a literal or %hi/@ha/@h macro, found {arg}"
            )
        ref = parse_symbol_ref(arg.argument)
        if ref is None:
            raise DecompFailure(f"Invalid macro argument {arg.argument}")
        return ref

    def maybe_gprel_imm(self, index: int) -> Optional[RawSymbolRef]:
        val = self.raw_arg(index)
        if not isinstance(val, Macro) or val.macro_name != "gp_rel":
            return None
        ref = parse_symbol_ref(val.argument)
        if ref is None:
            raise DecompFailure(f"Invalid macro argument {val.argument}")
        return ref

    def maybe_got_imm(self, index: int) -> Optional[RawSymbolRef]:
        arg = self.raw_arg(index)
        if not isinstance(arg, AsmAddressMode) or arg.base != Register("gp"):
            return None
        val = arg.addend
        if not isinstance(val, Macro) or val.macro_name not in (
            "got",
            "call16",
        ):
            return None
        ref = parse_symbol_ref(val.argument)
        if ref is None:
            raise DecompFailure(f"Invalid macro argument {val.argument}")
        return ref

    def shifted_u16_imm(self, index: int) -> Expression:
        # TODO: Should this be part of hi_imm? Do we need to handle @ha?
        raw_imm = self.u16_imm(index)
        assert isinstance(raw_imm, Literal)
        return Literal(raw_imm.value << 16)

    def sym_imm(self, index: int) -> Expression:
        arg = self.raw_arg(index)
        if isinstance(arg, AsmGlobalSymbol):
            return self.stack_info.global_info.address_of_gsym(arg.symbol_name)
        if isinstance(arg, AsmLiteral):
            return self.full_imm(index)
        raise DecompFailure(f"Bad function call operand {arg}")

    def memory_ref(self, index: int) -> Union[AddressMode, RawSymbolRef]:
        ret = strip_macros(self.raw_arg(index))

        # For MIPS, we want to allow "lw $v0, symbol + 4", which is outputted by
        # some disassemblers (like IDA) even though it isn't valid assembly.
        # We also want to allow "lw $v0, %gp_rel(symbol + 4)($gp)", where $gp is
        # assumed to point to the start of a small data area.
        # (strip_macros removes the AsmAddressMode wrapper for gp_rel relocs.)
        # PPC sda2/sda21 relocs also behave this way.
        ref = parse_symbol_ref(ret)
        if ref is not None:
            return ref

        if not isinstance(ret, AsmAddressMode):
            raise DecompFailure(
                "Expected instruction argument to be of the form offset($register), "
                f"but found {ret}"
            )
        if not isinstance(ret.addend, AsmLiteral):
            raise DecompFailure(
                f"Unable to parse offset for instruction argument {ret}. "
                "Expected a constant or a %lo macro."
            )
        return AddressMode(offset=ret.addend.value, base=ret.base)

    def count(self) -> int:
        return len(self.raw_args)


def is_trivial_expression(expr: Expression) -> bool:
    """Compute whether an EvalOnceExpr should be marked as `trivial = True`."""
    # NaivePhiExpr could be made trivial, but it's better to keep it symmetric
    # with PlannedPhiExpr to avoid different deduplication between passes,
    # since that can cause naive phis to end up in the final output.
    if isinstance(expr, (Literal, GlobalSymbol, SecondF64Half)):
        return True
    if isinstance(expr, AddressOf):
        return all(is_trivial_expression(e) for e in expr.dependencies())
    return False


def should_wrap_transparently(expr: Expression) -> bool:
    """Compute whether an EvalOnceExpr should be marked as `transparent = True`,
    thus not using a variable despite multiple uses (unless forced)."""
    if isinstance(
        expr,
        (
            Literal,
            LocalVar,
            PassedInArg,
            NaivePhiExpr,
            PlannedPhiExpr,
            SecondF64Half,
            SubroutineArg,
        ),
    ):
        return True
    if isinstance(expr, EvalOnceExpr):
        return not expr.is_fictive_temp
    if isinstance(expr, GlobalSymbol):
        return not expr.is_string_constant()
    if isinstance(expr, AddressOf):
        return should_wrap_transparently(expr.expr)
    if isinstance(expr, Cast):
        return expr.should_wrap_transparently()
    if (
        isinstance(expr, StructAccess)
        and isinstance(expr.struct_var, AddressOf)
        and isinstance(expr.struct_var.expr, GlobalSymbol)
        and should_wrap_transparently(expr.struct_var.expr)
    ):
        # Don't emit temps for reads of globals: they are usually compiler
        # generated, and prevent_later_var_uses/prevent_later_reads tend to be
        # good enough at turning wrappers non-transparent when they aren't that
        # we are fine being a bit liberal here.
        return True
    return False


def is_type_obvious(expr: Expression) -> bool:
    """
    Determine whether an expression's type is "obvious", e.g. because the
    expression refers to a variable which has a declaration. With perfect type
    information this this function would not be needed.

    This function may produce wrong results while code is being generated,
    since at that point we don't know the final status of EvalOnceExpr's.
    """
    if isinstance(
        expr,
        (
            Cast,
            Literal,
            AddressOf,
            LocalVar,
            NaivePhiExpr,
            PassedInArg,
            PlannedPhiExpr,
            FuncCall,
        ),
    ):
        return True
    if isinstance(expr, EvalOnceExpr):
        if expr.uses_var():
            return True
        return is_type_obvious(expr.wrapped_expr)
    return False


def simplify_condition(expr: Expression) -> Expression:
    """
    Simplify a boolean expression.

    This function may produce wrong results while code is being generated,
    since at that point we don't know the final status of EvalOnceExpr's.
    """
    if isinstance(expr, EvalOnceExpr) and not expr.uses_var():
        return simplify_condition(expr.wrapped_expr)
    # (No Mercy Arcade, 2026-11-10) Only what is itself tested for true or false is simplified as a condition: the operand of
    # `!`, both sides of `&&` and `||`, and the left side of `== 0` / `!= 0`. This used to go into every operand, so the
    # `!= 0` of a value that is then ANDed, negated or compared as a number was dropped too. func_801075DC has
    #     and $v1, $a1, 0x28000;  sltu $v1, $zero, $v1;  xori/sltiu $v0 = (s4 == 3);  and $v1, $v1, $v0;  beq $v1, $zero
    # which is ((a1 & 0x28000) != 0) & (s4 == 3), and came out as `a1 & 0x28000 & (arg2 == 3)`: always 0, since the mask has
    # no bit 0. The value of `x != 0` is 0 or 1; `x` is not.
    def value(e: Expression) -> Expression:
        # (As simplify_condition unwraps a value with no variable of its own, without rewriting what is inside.)
        while isinstance(e, EvalOnceExpr) and not e.uses_var():
            e = e.wrapped_expr
        return e
    if isinstance(expr, UnaryOp):
        if expr.op != "!":
            return expr
        inner = simplify_condition(expr.expr)
        if expr.op == "!" and isinstance(inner, Condition):
            return inner.negated()
        return UnaryOp(expr=inner, op=expr.op, type=expr.type)
    if isinstance(expr, BinaryOp):
        if expr.op in ("&&", "||"):
            left = simplify_condition(expr.left)
            right = simplify_condition(expr.right)
        elif expr.op in ("==", "!=") and value(expr.right) == Literal(0):
            left = simplify_condition(expr.left)
            right = value(expr.right)
        else:
            left = value(expr.left)
            right = value(expr.right)
        if isinstance(left, UnaryOp) and left.op == "!" and right == Literal(0):
            # `!(expr) == 0` is equivalent to `expr`
            if expr.op == "==":
                return left.expr
            # `!(expr) != 0` is equivalent to `!expr`
            if expr.op == "!=":
                return left
        if (
            isinstance(left, BinaryOp)
            and (left.is_comparison() or left.op == "&")
            and right == Literal(0)
        ):
            if expr.op == "==":
                return simplify_condition(left.negated())
            if expr.op == "!=":
                return left
        if (
            expr.is_comparison()
            and isinstance(left, Literal)
            and not isinstance(right, Literal)
        ):
            return BinaryOp(
                left=right,
                op=expr.op.translate(str.maketrans("<>", "><")),
                right=left,
                type=expr.type,
                cmp=expr.cmp,
            )

        # (2026-11-13, the ARM64 proof: `div` kept. A division used straight in a condition, `if ((a % b) == 0)`, went through
        #  here and lost its mark, so --n64-division left it as C++'s %: func_800F1634, func_8011DCD8, func_801366E4,
        #  modrep_func_8001FFA8.)
        return BinaryOp(left=left, op=expr.op, right=right, type=expr.type, cmp=expr.cmp, div=expr.div)
    return expr


def balanced_parentheses(string: str) -> bool:
    """
    Check if parentheses in a string are balanced, ignoring any non-parenthesis
    characters. E.g. true for "(x())yz", false for ")(" or "(".
    """
    bal = 0
    for c in string:
        if c == "(":
            bal += 1
        elif c == ")":
            if bal == 0:
                return False
            bal -= 1
    return bal == 0


def format_expr(expr: Expression, fmt: Formatter) -> str:
    """Stringify an expression, stripping unnecessary parentheses around it."""
    ret = expr.format(fmt)
    if ret.startswith("(") and balanced_parentheses(ret[1:-1]):
        return ret[1:-1]
    return ret


def may_print_as_pointer(expr: Expression, fmt: Formatter) -> bool:
    """No Mercy Arcade, 2026-11-07: whether expr, as printed, may have a pointer type in the C. m2c's own type for it can say
    a number while the C adds a number to a variable declared as a pointer (`temp_s4 + 0xFC`), so this follows what's
    printed: a name has its declared type, a cast that's printed its type, + and - a pointer if a side is (pointer - pointer
    is a number). Erring towards yes only costs a cast that changes nothing."""
    uw = late_unwrap(expr)
    if isinstance(uw, EvalOnceExpr) or isinstance(uw, PlannedPhiExpr):
        return uw.var.type.is_pointer_or_array()
    if isinstance(uw, Cast):
        if uw.format(fmt).startswith("(" + uw.type.format(fmt) + ")"):
            return uw.type.is_pointer_or_array()
        return may_print_as_pointer(uw.expr, fmt)
    if isinstance(uw, BinaryOp) and uw.op in ("+", "-"):
        left = may_print_as_pointer(uw.left, fmt)
        right = may_print_as_pointer(uw.right, fmt)
        if uw.op == "-":
            return left and not right
        return left or right
    if isinstance(uw, BinaryOp):
        return False
    return uw.type.is_pointer_or_array()


def format_assignment(
    dest: Union[Expression, Var], source: Expression, fmt: Formatter
) -> str:
    """Stringify `dest = source;`."""
    is_dest: Callable[[Expression], bool]
    if isinstance(dest, Expression):
        dest = late_unwrap(dest)
        is_dest = lambda e: e == dest
    else:
        is_dest = lambda e: var_for_expr(e) == dest
    source = late_unwrap(source)
    # (Not a division written LIFT_DIV(a, b), --n64-division: it has no compound form.)
    if isinstance(source, BinaryOp) and source.op in COMPOUND_ASSIGNMENT_OPS and not (N64_DIVISION and source.div is not None):
        source = source.normalize_for_formatting()
        rhs = None
        if is_dest(late_unwrap(source.left)):
            rhs = source.right
        elif is_dest(late_unwrap(source.right)) and source.op in ASSOCIATIVE_OPS:
            rhs = source.left
        if rhs is not None:
            return f"{dest.format(fmt)} {source.op}= {format_expr(rhs, fmt)};"
    # No Mercy Arcade, 2026-11-07: an address assigned to a variable printed as a number (`temp_s3 = temp_s4 + 0xFC;`, temp_s3
    # an s32 and temp_s4 a pointer) gets a cast to the variable's own type. C converts it that way without one; C++ (with
    # game pointers as a class) needs it written. A cast to the type being assigned is what the assignment does anyway.
    dest_type = dest.type if isinstance(dest, (Var, Expression)) else None
    if (
        fmt.valid_syntax
        and dest_type is not None
        and re.fullmatch(r"[su](8|16|32)|M2C_UNK(8|16|32)?", dest_type.format(fmt))
        and may_print_as_pointer(source, fmt)
    ):
        return f"{dest.format(fmt)} = ({dest_type.format(fmt)}) ({format_expr(source, fmt)});"
    return f"{dest.format(fmt)} = {format_expr(source, fmt)};"


def var_for_expr(expr: Expression) -> Optional[Var]:
    """If an expression *may* expand to the read of a Var, return that var.

    EvalOnceExpr's with `transparent = True`, (which may become non-transparent
    in the future) return their underlying var."""
    if isinstance(expr, PlannedPhiExpr):
        return expr.var
    if isinstance(expr, EvalOnceExpr) and not expr.trivial:
        return expr.var
    return None


def parenthesize_for_struct_access(expr: Expression, fmt: Formatter) -> str:
    # Nested dereferences may need to be parenthesized. All other
    # expressions will already have adequate parentheses added to them.
    s = expr.format(fmt)
    if (
        s.startswith("*")
        or s.startswith("&")
        or (isinstance(expr, Cast) and expr.needed_for_store())
    ):
        return f"({s})"
    return s


def elide_literal_casts(expr: Expression) -> Expression:
    uw_expr = late_unwrap(expr)
    if isinstance(uw_expr, Cast) and not uw_expr.needed_for_store():
        return elide_literal_casts(uw_expr.expr)
    if isinstance(uw_expr, Literal) and uw_expr.type.is_int() and uw_expr.value >= 0:
        # Avoid suffixes for non-negative unsigned ints
        return replace(uw_expr, elide_cast=True)
    return uw_expr


def uses_expr_sub(
    expr: Expression,
    expr_filter: Callable[[Expression], bool],
    is_parent_addressof: bool,
    may_move_forward: bool,
    seen: Set[Tuple[int, bool, bool]],
) -> bool:
    cache_key = (id(expr), may_move_forward, is_parent_addressof)
    if cache_key in seen:
        return False
    seen.add(cache_key)
    if expr_filter(expr) and not is_parent_addressof:
        return True
    if isinstance(expr, EvalOnceExpr):
        e = expr.dependency(may_move_forward)
        return e is not None and (
            uses_expr_sub(
                e, expr_filter, False, may_move_forward or expr.emit_exactly_once, seen
            )
        )
    elif isinstance(expr, AddressOf):
        return uses_expr_sub(expr.expr, expr_filter, True, may_move_forward, seen)
    else:
        for e in expr.dependencies():
            if uses_expr_sub(e, expr_filter, False, may_move_forward, seen):
                return True
        return False


def pin_pending(
    expr: Expression, expr_filter: Callable[[Expression], bool], held: bool = False, calls: bool = False,
    reassigned: bool = False,
) -> bool:
    """No Mercy Arcade, 2026-11-07: makes every part of a value not yet written out that matches expr_filter keep the value it
    has now: the innermost value that holds it and isn't yet a variable (an EvalOnceExpr) gets its temp, assigned where that
    value was made. Returns False if a match isn't inside any such value (it would be printed later, as it is then).

    Unlike uses_expr, this doesn't stop at a value already used once (EvalOnceExpr.dependency's "a later use makes a
    variable anyway"): that use may itself still be waiting, inside another value or a branch condition. A value that is a
    variable already, or a call (emitted once, where it is; prevent_later_function_calls keeps calls in order), is settled.

    held: expr is a value held in a register (or kept for a later call or restore), not part of a waiting expression. Such a
    value already used once, and not transparent, is settled too: that use is either written out already or inside another
    waiting value (pinned from there), and any further use makes it a variable assigned where it was made. A transparent one
    (a stack local's read, sp1C) is printed again at each use, so it's pinned like the rest.

    calls: also pin calls -- a call's result used once is printed where it's used, so the call itself moves there; pinned,
    it's written as `temp_ret = f(...)` where it was made.

    reassigned: what's being changed is a variable itself (the instruction now being translated assigns it), so a part of
    the value that reads that variable is a match even though it is a variable already: it would print the new value. It
    can't be pinned where it is (it IS the variable), so the value holding it is pinned instead, or this fails. Only for a
    branch condition or switch, which is written out after the delay slot (pin_pending_control); see there.""" 
    seen: Dict[int, bool] = {}
    if held and isinstance(expr, EvalOnceExpr) and expr.is_used and not expr.transparent:
        return True

    def walk(e: Expression) -> bool:
        if id(e) not in seen:
            seen[id(e)] = True      # (while it's being walked: values don't contain themselves)
            seen[id(e)] = walk_once(e)
        return seen[id(e)]

    def walk_once(e: Expression) -> bool:
        if isinstance(e, EvalOnceExpr):
            if calls and e.emit_exactly_once and not e.uses_var():
                # (The call itself, used yet or not -- not what holds its result: see pin_pending_calls.)
                e.force()
                return True
            if reassigned and e.uses_var() and expr_filter(e):
                return False
            if e.emit_exactly_once and not e.uses_var():
                # (2026-11-08) A call not written out yet is printed where its result is used -- in a branch condition, after
                # the delay slot -- and its arguments are read there. If a match is inside them, the call is pinned to its
                # temp where it was made. func_800E66E4: `jal func_8000A830` with a1 the byte at +4, then `beq ...; addu $a1,
                # $s0, $zero` in the delay slot, came out as `var_a1 = temp_s0_2; if (f(func_8000A830(temp_v0, var_a1)) != 0)`,
                # the call given the byte at +5.
                if not walk(e.wrapped_expr):
                    e.force()
                return True
            if e.uses_var() or e.emit_exactly_once:
                return True
            # The innermost value is pinned: what's inside it first, and this one only if something in it is a match
            # that no value inside can hold. (Pinning an outer value hid it from m2c's pattern folds, which look through
            # values not yet variables: func_8013E51C's `mfhi` division got a temp before its sign fix-up `- (x >> 31)` was
            # folded in, and came out divided and fixed up twice.)
            if walk(e.wrapped_expr):
                return True
            if not e.trivial:
                e.force()
                return True
            return False
        if isinstance(e, AddressOf):
            return all(walk(d) for d in e.expr.dependencies())
        if expr_filter(e):
            return False
        return all(walk(d) for d in e.dependencies())

    return walk(expr)


# (The values stale in the block being translated: NodeState.stale, by id. None when no block is.)
stale_here: Optional[Dict[int, Expression]] = None


def is_stale(expr: "EvalOnceExpr") -> bool:
    """Whether a value is stale (mark_stale) where the translation now is. A value made in one block is seen by every block
    after it; what changes on one branch has not changed on the other, so while blocks are being translated it is the
    current block's own marks that count (they start as those its parent block ended with). Afterwards, when the C is
    put together and written, a value marked anywhere counts."""
    if stale_here is not None:
        return id(expr) in stale_here
    return expr.stale


def mark_stale(
    exprs: List[Expression], expr_filter: Callable[[Expression], bool], here: Dict[int, Expression]
) -> None:
    """No Mercy Arcade, 2026-11-11, trial 5: marks as stale every value in exprs that was made from something matching
    expr_filter, which is about to change (a variable assigned again, memory written, a call made).

    m2c's own guard, _prevent_later_uses, stops at a value that has been used once or has its variable: "any further use
    makes it a variable, assigned where it was made", so it needs no guarding. That holds for a use of the value itself.
    But early_unwrap looks inside such a value for its parts, and a part taken out is printed where the new use is, as it
    is then. func_80031AAC:

        addiu $s0, $s0, 8       temp_s0 = var_s0 + 8;
        addu  $a2, $s0, $zero
        addiu $s0, $s0, 8       var_s0 = temp_s0 + 8;
        sw    $v1, 0x0($a2)     M2C_FIELD(var_s0, s32 *, 8) = ...;     <- 16 bytes on: should be *temp_s0

    The store's address, temp_s0, was looked into (evaluate.py deref, "*(x + offset)") and written as field 8 of var_s0,
    after var_s0 had been assigned again. A stale value is not looked into, so the store is written through temp_s0.

    What is marked: a value with a match among its own parts, not counting what is inside another value that is settled
    (used once and not transparent, or a variable already). A settled value inside is marked itself, and is safe to take
    out of the one around it: used again, it becomes its variable. A value not settled passes the mark up to the one
    holding it, since printed anywhere later it would read the changed thing."""
    seen: Dict[Tuple[int, bool], bool] = {}

    def walk(e: Expression, under_addressof: bool = False) -> bool:
        key = (id(e), under_addressof)
        if key not in seen:
            seen[key] = False       # (while it's being walked: values don't contain themselves)
            seen[key] = walk_once(e, under_addressof)
        return seen[key]

    def walk_once(e: Expression, under_addressof: bool) -> bool:
        # (As uses_expr: `&x` doesn't read x.)
        if expr_filter(e) and not under_addressof:
            return True
        if isinstance(e, EvalOnceExpr):
            inside = walk(e.wrapped_expr)
            if e.trivial:
                return inside
            if inside:
                e.stale = True
                here[id(e)] = e
            settled = e.var.is_emitted or (e.is_used and not e.transparent)
            return inside and not settled
        if isinstance(e, AddressOf):
            return walk(e.expr, True)
        # (Every part is walked, not just up to the first match: each may hold values to mark.)
        return any([walk(d) for d in e.dependencies()])

    for expr in exprs:
        if not isinstance(expr, (PlannedPhiExpr, NaivePhiExpr)):
            walk(expr)


def uses_expr(
    expr: Expression,
    expr_filter: Callable[[Expression], bool],
) -> bool:
    return uses_expr_sub(
        expr,
        expr_filter,
        is_parent_addressof=False,
        may_move_forward=False,
        seen=set(),
    )


def late_unwrap(expr: Expression) -> Expression:
    """
    Unwrap EvalOnceExpr's, stopping at variable boundaries.

    This function may produce wrong results while code is being generated,
    since at that point we don't know the final status of EvalOnceExpr's.
    """
    if isinstance(expr, EvalOnceExpr) and not expr.uses_var():
        return late_unwrap(expr.wrapped_expr)
    if isinstance(expr, NaivePhiExpr) and expr.replacement_expr is not None:
        return late_unwrap(expr.replacement_expr)
    return expr


def transparent_unwrap(expr: Expression) -> Expression:
    """
    Unwrap transparent EvalOnceExpr's (e.g. EvalOnceExpr's that wrap other
    EvalOnceExpr's, or trivial EvalOnceExpr's).

    This is safe to use but may result in suboptimal codegen if used while code
    is being generated, since wrapper transparency is not yet final. (In
    particular, it may result in an inconsistent choice of temps to refer to.)
    """
    if isinstance(expr, EvalOnceExpr) and expr.transparent and not expr.uses_var():
        return transparent_unwrap(expr.wrapped_expr)
    return expr


def early_unwrap(expr: Expression) -> Expression:
    """
    Unwrap EvalOnceExpr's, even past variable boundaries.

    This is fine to use even while code is being generated, but disrespects decisions
    to use a temp for a value, so use with care.
    """
    if (
        isinstance(expr, EvalOnceExpr)
        and not expr.forced_emit
        and not expr.emit_exactly_once
        and not is_stale(expr)      # (2026-11-11, trial 5: its parts aren't what they were -- mark_stale)
    ):
        return early_unwrap(expr.wrapped_expr)
    return expr


def early_unwrap_whole(expr: Expression) -> Expression:
    """No Mercy Arcade, 2026-11-11, trial 5: early_unwrap as it was before values could be stale (mark_stale): through
    those too. Only for telling which place an access is at (deref gives every access to one place a single type), never
    for a part that will be printed."""
    if (
        isinstance(expr, EvalOnceExpr)
        and not expr.forced_emit
        and not expr.emit_exactly_once
    ):
        return early_unwrap_whole(expr.wrapped_expr)
    return expr


def early_unwrap_ints(expr: Expression) -> Expression:
    """
    Unwrap EvalOnceExpr's, even past variable boundaries or through int Cast's
    This is a bit sketchier than early_unwrap(), but can be used for pattern matching.
    """
    uw_expr = early_unwrap(expr)
    if isinstance(uw_expr, Cast) and uw_expr.reinterpret and uw_expr.type.is_int():
        return early_unwrap_ints(uw_expr.expr)
    return uw_expr


def late_unwrap_ints(expr: Expression) -> Expression:
    """Like early_unwrap_ints, but with late_unwrap."""
    uw_expr = late_unwrap(expr)
    if isinstance(uw_expr, Cast) and uw_expr.reinterpret and uw_expr.type.is_int():
        return late_unwrap_ints(uw_expr.expr)
    return uw_expr


def unwrap_deep(expr: Expression) -> Expression:
    """
    Unwrap EvalOnceExpr's, even past variable boundaries.

    This is generally a sketchy thing to do, try to avoid it. In particular:
    - the returned expression is not usable for emission, because it may contain
      accesses at an earlier point in time or an expression that should not be repeated.
    - just because unwrap_deep(a) == unwrap_deep(b) doesn't mean a and b are
      interchangable, because they may be computed in different places.
    """
    if isinstance(expr, EvalOnceExpr):
        return unwrap_deep(expr.wrapped_expr)
    return expr


def literal_expr(arg: Argument, stack_info: StackInfo) -> Expression:
    if isinstance(arg, AsmGlobalSymbol):
        return stack_info.global_info.address_of_gsym(arg.symbol_name)
    if isinstance(arg, AsmLiteral):
        return Literal(arg.value)
    if isinstance(arg, BinOp):
        lhs = literal_expr(arg.lhs, stack_info)
        rhs = literal_expr(arg.rhs, stack_info)
        return BinaryOp.int(left=lhs, op=arg.op, right=rhs)
    raise DecompFailure(f"Instruction argument {arg} must be a literal")


def parse_symbol_ref(arg: Argument) -> Optional[RawSymbolRef]:
    if isinstance(arg, AsmGlobalSymbol):
        return RawSymbolRef(offset=0, sym=arg)
    if (
        isinstance(arg, BinOp)
        and arg.op in "+-"
        and isinstance(arg.lhs, AsmGlobalSymbol)
        and isinstance(arg.rhs, AsmLiteral)
    ):
        return RawSymbolRef(
            offset=arg.rhs.value * (-1 if arg.op == "-" else 1), sym=arg.lhs
        )
    return None


def format_f32_imm(num: int) -> str:
    packed = struct.pack(">I", num & (2**32 - 1))
    value = struct.unpack(">f", packed)[0]

    if not value or value == 4294967296.0:
        # Zero, negative zero, nan, or 2**32.
        return str(value)

    # Write values smaller than 1e-7 / greater than 1e7 using scientific notation,
    # and values in between using fixed point.
    if abs(math.log10(abs(value))) > 6.9:
        fmt_char = "e"
    elif abs(value) < 1:
        fmt_char = "f"
    else:
        fmt_char = "g"

    def fmt(prec: int) -> str:
        """Format 'value' with 'prec' significant digits/decimals, in either scientific
        or regular notation depending on 'fmt_char'."""
        ret = ("{:." + str(prec) + fmt_char + "}").format(value)
        if fmt_char == "e":
            return ret.replace("e+", "e").replace("e0", "e").replace("e-0", "e-")
        if "e" in ret:
            # The "g" format character can sometimes introduce scientific notation if
            # formatting with too few decimals. If this happens, return an incorrect
            # value to prevent the result from being used.
            #
            # Since the value we are formatting is within (1e-7, 1e7) in absolute
            # value, it will at least be possible to format with 7 decimals, which is
            # less than float precision. Thus, this annoying Python limitation won't
            # lead to us outputting numbers with more precision than we really have.
            return "0"
        return ret

    # 20 decimals is more than enough for a float. Start there, then try to shrink it.
    prec = 20
    while prec > 0:
        prec -= 1
        value2 = float(fmt(prec))
        if struct.pack(">f", value2) != packed:
            prec += 1
            break

    if prec == 20:
        # Uh oh, even the original value didn't format correctly. Fall back to str(),
        # which ought to work.
        return str(value)

    ret = fmt(prec)
    if "." not in ret and "e" not in ret:
        ret += ".0"
    return ret


def format_f64_imm(num: int) -> str:
    (value,) = struct.unpack(">d", struct.pack(">Q", num & (2**64 - 1)))
    return str(value)


def strip_macros(arg: Argument) -> Argument:
    """Replace %lo(...) by 0, and assert that there are no %hi(...). We assume that
    %hi's only ever occur in lui, where we expand them to an entire value, and not
    just the upper part. This preserves semantics in most cases (though not when %hi's
    are reused for different %lo's...)"""
    if isinstance(arg, Macro):
        if arg.macro_name in ["sda2", "sda21"]:
            return arg.argument
        if arg.macro_name == "hi":
            raise DecompFailure("%hi macro outside of lui")
        if arg.macro_name not in ["lo", "l"]:
            raise DecompFailure(f"Unrecognized linker macro %{arg.macro_name}")
        # This is sort of weird; for `symbol@l` we return 0 here and assume
        # that this @l is always perfectly paired with one other @ha.
        # However, with `literal@l`, we return the literal value, and assume it is
        # paired with another `literal@ha`. This lets us reuse `literal@ha` values,
        # but assumes that we never mix literals & symbols
        if isinstance(arg.argument, AsmLiteral):
            return AsmLiteral(arg.argument.value)
        return AsmLiteral(0)
    elif isinstance(arg, AsmAddressMode) and isinstance(arg.addend, Macro):
        if arg.addend.macro_name in ["sda2", "sda21", "gp_rel"]:
            return arg.addend.argument
        if arg.addend.macro_name not in ["lo", "l"]:
            raise DecompFailure(
                f"Bad linker macro in instruction argument {arg}, expected %lo"
            )
        return AsmAddressMode(
            base=arg.base,
            addend=AsmLiteral(0),
            writeback=arg.writeback,
        )
    else:
        return arg


def find_clobbers_until_dominator(
    stack_info: StackInfo, node: Node
) -> Tuple[Set[Var], bool]:
    assert node.immediate_dominator is not None

    seen = {node.immediate_dominator}
    stack = node.parents[:]
    clobbered_vars = set()
    has_fn_call = False
    while stack:
        n = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        for instr_ref in n.block.instruction_refs:
            instr = instr_ref.instruction
            for loc in instr.outputs:
                if isinstance(loc, Register):
                    var = stack_info.get_planned_var(loc, instr_ref)
                    if var is not None:
                        clobbered_vars.add(var)
            if instr.function_target is not None:
                has_fn_call = True
        stack.extend(n.parents)
    return clobbered_vars, has_fn_call


def reg_sources(node: Node, reg: Register) -> Tuple[List[Reference], bool]:
    assert node.immediate_dominator is not None
    seen = {node.immediate_dominator}
    stack = node.parents[:]
    sources: List[Reference] = []
    uses_dominator = False
    while stack:
        n = stack.pop()
        if n == node.immediate_dominator:
            uses_dominator = True
        if n in seen:
            continue
        seen.add(n)
        for instr_ref in reversed(list(n.block.instruction_refs)):
            instr = instr_ref.instruction
            if reg in instr.outputs:
                sources.append(instr_ref)
                break
            elif reg in instr.clobbers:
                return [], False
        else:
            stack.extend(n.parents)
    return sources, uses_dominator


def reads_memory(expr: Expression) -> bool:
    """No Mercy Arcade (2026-11-08): whether evaluating this expression reads memory -- a field, an array element, a frame slot or
    stack argument, an unaligned load -- or calls a function, anywhere in it. (A phi inside it is a value already made, not read
    again.) For assign_naive_phis."""
    if isinstance(expr, EvalOnceExpr):
        return reads_memory(expr.wrapped_expr)
    if isinstance(expr, (NaivePhiExpr, PlannedPhiExpr)):
        return False
    if isinstance(expr, AddressOf):
        # (A field's or a frame slot's address reads nothing there: only what the address is made from counts.)
        inner = expr.expr
        if isinstance(inner, StructAccess):
            return reads_memory(inner.struct_var)
        if isinstance(inner, (LocalVar, GlobalSymbol)):
            return False
        return reads_memory(inner)
    if isinstance(
        expr,
        (StructAccess, ArrayAccess, LocalVar, FrameArgSlot, FuncCall, Lwl, Load3Bytes, UnalignedLoad, SubroutineArg),
    ):
        return True
    return any(reads_memory(e) for e in expr.dependencies())


def reads_phi_variable(expr: Expression) -> bool:
    """No Mercy Arcade (2026-11-10): whether this expression, as it would be printed, names a variable that is assigned in
    more than one place -- a phi's variable, or a value assigned to one -- anywhere in it. For assign_naive_phis."""
    if isinstance(expr, (NaivePhiExpr, PlannedPhiExpr)):
        return True
    if isinstance(expr, EvalOnceExpr):
        return expr.var.is_planned or reads_phi_variable(expr.wrapped_expr)
    return any(reads_phi_variable(e) for e in expr.dependencies())


def assign_naive_phis(
    used_naive_phis: List[NaivePhiExpr], stack_info: StackInfo
) -> None:
    i = 0
    # Iterate over used phis until there are no more remaining. New ones may
    # appear during iteration, hence the while loop.
    while i < len(used_naive_phis):
        phi = used_naive_phis[i]
        assert phi.num_usages > 0
        assert len(phi.sources) >= 2
        assert len(phi.node.parents) >= 2

        # Group parent nodes by the value of their phi register
        equivalent_blocks: DefaultDict[Expression, List[Block]] = defaultdict(list)
        for source in phi.sources:
            if isinstance(source, InstrRef):
                block = source.block
            else:
                # Use the end of the first block instead of the start of the
                # function. Since there's no write in between both are fine.
                # (If we were a write in between it would be a source.)
                block = stack_info.flow_graph.entry_node().block
            expr = get_block_info_for_block(block).final_register_states[phi.reg]
            expr.type.unify(phi.type)
            equivalent_blocks[transparent_unwrap(expr)].append(block)

        exprs = list(equivalent_blocks.keys())
        first_uw = early_unwrap(exprs[0])
        # (No Mercy Arcade, 2026-11-08) Not when the value is read from memory and comes from more than one place: the
        # replacement is the read itself, written where the phi is used, so it reads memory there -- after whatever the
        # function wrote or called since each of the reads the original made (m2c's own note below: "we may end up with
        # incorrect output"). func_800340E0's loop keeps a list pointer in s0, loaded before the loop and again at its end
        # (`lw $s0, 0x14($s1)`), and passes it to two calls; the two loads are alike, so the pointer became
        # `M2C_FIELD(arg0, 0x14)` at each call, read again after the first call had changed the list. With a memory read in
        # it, the phi is a variable, set where each read was, as for values that differ.
        same = all(early_unwrap(e) == first_uw for e in exprs[1:])
        # (2026-11-10) And when the reads are alike enough to be one entry of equivalent_blocks -- the same frame slot read in
        # both arms of an if, `lw $s0, 0x30($sp)` twice: func_8012CEE8. Then exprs has one entry though the value still comes
        # from two reads, and the test above (len(exprs) > 1) let it through: the phi became `sp30` itself, written where s0 is
        # used, after the function had stored `sp30 + 8` back to the slot -- so `sw $v1, 0x0($s0)` wrote 8 bytes too far
        # (46,556 of 61,961 tests DIFFERENT). A phi always has two sources or more (the assert above), so any memory read
        # here is more than one read: a variable, set where each read was.
        if same and reads_memory(first_uw):
            same = False
        # (2026-11-10) Nor when it is a variable that is assigned in more than one place (a phi's own variable), or made from
        # one: written where the phi is used, it is that variable as it is THEN. modrep_func_8001FFA8's loop sets a0 from s1
        # on two ways into a call (`addu $a0, $s1, $zero` twice) and steps s1 in the call's delay slot (`jal func_80010FC4;
        # addiu $s1, $s1, 0xDC`). The two a0's are alike, so a0 became `var_s1` itself, and the call -- written after the delay
        # slot's `var_s1 += 0xDC` -- was given the pointer one entry on. Such a phi is a variable too.
        if same and reads_phi_variable(first_uw):
            same = False
        if same:
            # All the phis have the same value (e.g. because we recomputed an
            # expression after a store, or restored a register after a function
            # call). Just use that value instead of introducing a phi node.
            #
            # The early_unwrap's here are kinda sketchy. It we left them out,
            # the transparent_unwrap's would still leave us able to detect
            # registers stored on stack across function calls, and literals
            # being rematerialized. What early_unwrap gives us is the ability
            # to detect equality of non-trivial expressions (in the sense of
            # is_trivial_expression) with eq=True -- most importantly struct/
            # global accesses -- and avoid phi nodes for those. This helps a
            # lot with detecting and undoing the pattern of compilers where
            # they cache values of struct accesses and invalidate the cache on
            # writes/function calls. However, it comes at a cost: the
            # `replacement_expr` forwarding happens too late to be part of the
            # prevent_later_uses machinery, so we may end up with incorrect
            # output.
            #
            # We also aren't generating a temp for the `replacement_expr`,
            # resulting in potentially a lot of repeated struct accesses. This
            # would be fixable by pre-allocating a temp at the top of the block
            # when creating the NaivePhiExpr and using that here; however, this
            # often makes output worse, since it effectively undoes the
            # heuristic detection of cached struct reads. (In addition these
            # pre-reserved temps that don't correspond to an instruction would
            # add a bit of a complexity.)
            #
            # To avoid repeated accesses and participate in prevent_later_uses
            # in the simple case where `early_unwrap` is not needed, we
            # special-case len(exprs) == 1 by skipping the deeper unwrapping and
            # -- if the dominator acts as one of the phi sources, which is true
            # most of the time -- marking the phi as directly inheritable from
            # the dominator during the next translation pass. If the dominator
            # is not involved we can also inherit the expression from an
            # arbitrary predecessor, as long as it's trivial.
            phi_expr = exprs[0] if len(exprs) == 1 else first_uw
            phi.replacement_expr = as_var_type(phi_expr, phi.type, stack_info.global_info.register_cuts)
            for _ in range(phi.num_usages):
                phi_expr.use()
            if len(exprs) == 1:
                if phi.uses_dominator:
                    stack_info.add_planned_inherited_phi(
                        phi.node, phi.reg, PhiInheritSource.DOMINATOR
                    )
                elif is_trivial_expression(exprs[0]):
                    stack_info.add_planned_inherited_phi(
                        phi.node, phi.reg, PhiInheritSource.ANY_PARENT
                    )
        else:
            for expr, blocks in equivalent_blocks.items():
                for block in blocks:
                    block_info = get_block_info_for_block(block)
                    expr = block_info.final_register_states[phi.reg]
                    expr.use()
                    typed_expr = as_var_type(expr, phi.type, stack_info.global_info.register_cuts)
                    block_info.to_write.append(SetNaivePhiStmt(phi, typed_expr))
        i += 1

    for phi in used_naive_phis:
        if not phi.replacement_expr:
            output_reg_name = stack_info.function.reg_formatter.format(phi.reg)
            prefix = f"phi_{output_reg_name}"
            if stack_info.global_info.deterministic_vars:
                prefix = f"{prefix}_{phi.node.block.index}"
            phi.name = stack_info.temp_var(prefix)
            stack_info.naive_phi_vars.append(phi)

            # Merge the phi source vars together for the next translation pass.
            var = stack_info.get_persistent_planned_var(phi.reg, phi.sources[0])
            for source in phi.sources[1:]:
                stack_info.get_persistent_planned_var(phi.reg, source).join(var)


def propagate_register_meta(nodes: List[Node], reg: Register) -> None:
    """Propagate RegMeta bits forwards/backwards."""
    non_terminal: List[Node] = [n for n in nodes if not isinstance(n, TerminalNode)]

    # Set `is_read` based on `read_inherited`.
    for n in non_terminal:
        if reg in get_block_info(n).final_register_states.read_inherited:
            for p in n.parents:
                par_meta = get_block_info(p).final_register_states.get_meta(reg)
                if par_meta:
                    par_meta.is_read = True

    # Propagate `is_read` backwards.
    todo = non_terminal[:]
    while todo:
        n = todo.pop()
        meta = get_block_info(n).final_register_states.get_meta(reg)
        for p in n.parents:
            par_meta = get_block_info(p).final_register_states.get_meta(reg)
            if (par_meta and not par_meta.is_read) and (
                meta and meta.inherited and meta.is_read
            ):
                par_meta.is_read = True
                todo.append(p)

    # Set `uninteresting` and propagate it, `function_return`, and `in_pattern` forwards.
    # Start by assuming inherited values are all set; they will get unset iteratively,
    # but for cyclic dependency purposes we want to assume them set.
    for n in non_terminal:
        meta = get_block_info(n).final_register_states.get_meta(reg)
        if meta:
            if meta.inherited:
                meta.uninteresting = True
                meta.function_return = True
                meta.in_pattern = True
            else:
                meta.uninteresting |= (
                    meta.is_read or meta.function_return or meta.in_pattern
                )

    todo = non_terminal[:]
    while todo:
        n = todo.pop()
        if isinstance(n, TerminalNode):
            continue
        meta = get_block_info(n).final_register_states.get_meta(reg)
        if not meta or not meta.inherited:
            continue
        all_uninteresting = True
        all_function_return = True
        all_in_pattern = True
        for p in n.parents:
            par_meta = get_block_info(p).final_register_states.get_meta(reg)
            if par_meta:
                all_uninteresting &= par_meta.uninteresting
                all_function_return &= par_meta.function_return
                all_in_pattern &= par_meta.in_pattern
        if meta.uninteresting and not all_uninteresting and not meta.is_read:
            meta.uninteresting = False
            todo.extend(n.children())
        if meta.function_return and not all_function_return:
            meta.function_return = False
            todo.extend(n.children())
        if meta.in_pattern and not all_in_pattern:
            meta.in_pattern = False
            todo.extend(n.children())


def determine_return_register(
    return_blocks: List[BlockInfo], fn_decl_provided: bool, arch: Arch
) -> Optional[Tuple[Register, bool]]:
    """Determine which of the arch's base_return_regs (i.e. v0, f0) is the most
    likely to contain the return value, or if the function is likely void.
    Returns a tuple (register, is float)."""

    def priority(block_info: BlockInfo, reg: Register) -> int:
        meta = block_info.final_register_states.get_meta(reg)
        if not meta:
            return 4
        if meta.uninteresting:
            return 2
        if meta.in_pattern:
            return 1
        if meta.function_return:
            return 0
        return 3

    if not return_blocks:
        return None

    best_reg: Optional[Tuple[Register, bool]] = None
    best_prio = -1
    for reg, floating in arch.base_return_regs:
        prios = [priority(b, reg) for b in return_blocks]
        max_prio = max(prios)
        if max_prio == 4:
            # Register is not always set, skip it
            continue
        if max_prio <= 2 and not fn_decl_provided:
            # Register is always read after being written, or comes from a
            # function call; seems unlikely to be an intentional return.
            # Skip it, unless we have a known non-void return type.
            continue
        if max_prio > best_prio:
            best_prio = max_prio
            best_reg = reg, floating
    return best_reg


@dataclass
class NodeState:
    node: Node
    stack_info: StackInfo = field(repr=False)
    regs: RegInfo = field(repr=False)

    local_var_writes: Dict[LocalVar, Tuple[Register, Expression, bool]] = field(
        default_factory=dict
    )
    subroutine_args: Dict[int, Expression] = field(default_factory=dict)
    # (No Mercy Arcade: the instruction each of those was stored with, sw or swc1 -- see make_function_call's extra arguments.)
    subroutine_arg_mnemonics: Dict[int, str] = field(default_factory=dict)
    in_pattern: bool = False
    # (No Mercy Arcade, 2026-11-11, trial 5: the values stale in this block, by id -- see is_stale.)
    stale: Dict[int, Expression] = field(default_factory=dict)

    to_write: List[Union[Statement]] = field(default_factory=list)
    branch_condition: Optional[Condition] = None
    switch_control: Optional[SwitchControl] = None
    has_function_call: bool = False

    def _eval_once(
        self,
        expr: Expression,
        *,
        emit_exactly_once: bool,
        transparent: bool,
        reg: Register,
        source: Reference,
    ) -> EvalOnceExpr:
        planned_var = self.stack_info.get_planned_var(reg, source)
        is_fictive_temp = False

        if "_fictive_" in reg.register_name:
            if isinstance(expr, EvalOnceExpr) and not expr.var.is_planned:
                # In the common case where a fictive reg transparently wraps an
                # EvalOnceExpr whose var does not overlap with anything else,
                # we can avoid the risk of new temps entirely. (Fictive registers
                # are only used as inputs to single instructions, so will never
                # be planned vars.)
                assert not emit_exactly_once
                assert not planned_var
                return expr
            # In the less common case, at least give the temp reg a nicer name.
            reg = Register(reg.register_name.split("_fictive_")[0])
            is_fictive_temp = True

        trivial = transparent and is_trivial_expression(expr)

        if emit_exactly_once:
            # (otherwise this will be marked used once is_used becomes true)
            expr.use()

        force = False
        if planned_var is not None:
            var = planned_var
            expr = as_var_type(expr, var.type, self.stack_info.global_info.register_cuts)
            self.prevent_later_var_uses({var})
            if uses_expr(expr, lambda e: var_for_expr(e) == var):
                force = True
        else:
            prefix = self.stack_info.function.reg_formatter.format(reg)
            if prefix == "condition_bit":
                prefix = "cond"
            temp_name = f"temp_{prefix}"
            if self.stack_info.global_info.deterministic_vars:
                temp_name = f"{temp_name}_{self.regs.get_instruction_lineno()}"
            var = Var(self.stack_info, temp_name, is_planned=False, type=expr.type)
            self.stack_info.temp_vars.append(var)

        expr = EvalOnceExpr(
            wrapped_expr=expr,
            var=var,
            type=expr.type,
            sources=[source],
            emit_exactly_once=emit_exactly_once,
            trivial=trivial,
            transparent=transparent,
            is_fictive_temp=is_fictive_temp,
        )
        stmt = EvalOnceStmt(expr)
        self.write_statement(stmt)
        var.debug_exprs.append(expr)

        if planned_var is not None:
            # Count the pre-planned var assignment as a use. (We could make
            # this lazy until the consuming PlannedPhiExpr is used, to avoid dead
            # assignments for phis that appear due to function call heuristics
            # but later vanish when type information improves. For now, we
            # don't.)
            var.is_emitted = True
            expr.use()
            if force:
                expr.force()

        return expr

    def _prevent_later_uses(self, expr_filter: Callable[[Expression], bool]) -> None:
        """Prevent later uses of registers that recursively contain something that
        matches a callback filter."""
        # (No Mercy Arcade, 2026-11-11, trial 5: and the values the checks below pass over as settled are kept from being
        #  looked into -- mark_stale.)
        mark_stale(
            [data.value for data in self.regs.contents.values()]
            + [write for _, write, _ in self.local_var_writes.values()]
            + list(self.subroutine_args.values()),
            expr_filter,
            self.stale,
        )
        for r, data in self.regs.contents.items():
            expr = data.value
            if isinstance(expr, (PlannedPhiExpr, NaivePhiExpr)):
                continue
            if not data.meta.force and uses_expr(expr, expr_filter):
                # Mark the register as "if used, emit the expression's once var".
                if not isinstance(expr, EvalOnceExpr):
                    assert_never(expr)

                self.regs.update_meta(r, replace(data.meta, force=True))

        # Do the same for values saved across function calls.
        for loc, (reg, write, force) in self.local_var_writes.items():
            if not force and uses_expr(write, expr_filter):
                self.local_var_writes[loc] = (reg, write, True)

        # And for stack arguments stored for a call that hasn't come yet (No Mercy Arcade, 2026-11-07). They are held as
        # expressions until the call, so without this a register or memory changed between the store and the call changed
        # the argument too: func_80114CBC stores *s1 as its fifth argument, steps s1 by 0x10, then calls, and m2c passed
        # the word after. An argument that would change is evaluated where it was stored (its once-var forced); one that
        # can't be is an error, which the pipeline refuses.
        for offset, arg in list(self.subroutine_args.items()):
            if uses_expr(arg, expr_filter):
                inner = arg
                while isinstance(inner, Cast):
                    inner = inner.expr
                if isinstance(inner, EvalOnceExpr) and not inner.trivial:
                    inner.force()
                else:
                    self.subroutine_args[offset] = ErrorExpr(
                        "Stack arg changed before the call"
                    )

        # And for the block's branch condition, which is written out after the branch's delay slot (No Mercy Arcade,
        # 2026-11-07, see pin_pending_control).
        self.pin_pending_control(expr_filter)

    def prevent_later_value_uses(self, sub_expr: Expression) -> None:
        """Prevent later uses of registers that recursively contain a given
        subexpression."""
        self._prevent_later_uses(lambda e: e == sub_expr)

    def prevent_later_var_uses(self, vars: Set[Var]) -> None:
        if vars:
            self._prevent_later_uses(lambda e: var_for_expr(e) in vars)

    def prevent_later_function_calls(self) -> None:
        """Prevent later uses of registers that recursively contain a function call."""
        self._prevent_later_uses(lambda e: isinstance(e, FuncCall))

    def prevent_later_reads(self) -> None:
        """Prevent later uses of registers that recursively contain a read."""
        self._prevent_later_uses(lambda e: isinstance(e, (StructAccess, ArrayAccess)))

    def pin_pending_reads(self) -> None:
        """No Mercy Arcade, 2026-11-07: every memory read not yet written out is given its temp now, where it was made, before
        a store or a call that could change the memory it reads.

        m2c holds a read as an expression until it's used, and only stops that when the use would come after a store to the
        same expression (prevent_later_value_uses) or after a call. A store through another pointer to the same memory, a store
        to a stack slot whose address was passed on, or a read that has already been used once inside another expression still
        waiting (EvalOnceExpr.dependency then says "no dependency") moved past it: func_8002CF14 read arg1+0xC after a store
        to var_s1+4. So here every read held anywhere -- registers, values kept across calls, stack arguments, the block's
        branch condition or switch -- is pinned (pin_pending). A read that can't be pinned turns that place into an error,
        which stops m2c for this function (the pipeline refuses it)."""
        # (Not a read of a jump table, `jtbl_...`: that's the ROM's constant data, which no store changes, and m2c finds the
        # switch by that read staying in its register -- pinning it lost func_801394BC's switch.)
        def jump_table(e: Expression) -> bool:
            e = early_unwrap(e)
            if isinstance(e, GlobalSymbol):
                return e.c_symbol_name.startswith("jtbl_")
            return any(jump_table(d) for d in e.dependencies()) if not isinstance(e, EvalOnceExpr) else False

        is_read = lambda e: (
            isinstance(e, LocalVar)
            or (isinstance(e, StructAccess) and not jump_table(e.struct_var))
            or (isinstance(e, ArrayAccess) and not jump_table(e.ptr))
        )
        pending = [data.value for data in self.regs.contents.values()]
        pending += [write for (_, write, _) in self.local_var_writes.values()]
        pending += list(self.subroutine_args.values())
        for expr in pending:
            if not pin_pending(expr, is_read, held=True):
                raise DecompFailure("A read can't be kept before the store or call after it")
        self.pin_pending_control(is_read)

    def pin_pending_calls(self) -> None:
        """No Mercy Arcade, 2026-11-07: every call whose result isn't written out yet gets its temp, before a load. m2c
        prints a call's result where it's used, so the call moves there -- past a read the original made after it:
        func_80033EEC's `M2C_FIELD(temp_a2, s32 *, 0x10) + func_80034174(...)`, with `lw $v1, 0x10($a2)` (and the read of
        sp18 giving a2) after the call, and C++ evaluates the two sides of + in either order. prevent_later_function_calls
        does this only for registers, and not for a result already used once inside another waiting value."""
        pending = [data.value for data in self.regs.contents.values()]
        pending += [write for (_, write, _) in self.local_var_writes.values()]
        pending += list(self.subroutine_args.values())
        # (The call itself is pinned, `temp_ret = f(...);`, and what holds its result refers to that -- even a call whose
        # result is never used, since that isn't known yet. Not through prevent_later_function_calls, which makes the
        # registers holding the result variables instead: m2c's division folds look through values that aren't variables
        # yet, and func_801336D4's `f(...) / 6` then lost its sign fix-up fold and came out corrected twice.)
        for expr in pending:
            pin_pending(expr, lambda e: False, calls=True)
        if self.branch_condition is not None:
            pin_pending(self.branch_condition, lambda e: False, calls=True)
        if self.switch_control is not None:
            pin_pending(self.switch_control.control_expr, lambda e: False, calls=True)

    def pin_pending_control(self, expr_filter: Callable[[Expression], bool]) -> None:
        """No Mercy Arcade, 2026-11-07: the block's branch condition (or switch) is set by the branch, and written out after
        everything in the block -- including the branch's delay slot, which comes after it here. So what the delay slot changes
        must not reach into it: func_800196F0 has `lhu $v0, 0($a3); bne $v0, $zero, ...; addiu $a3, $a3, 2`, and m2c wrote
        `var_a3 += 2; if (*var_a3 == 0)`. The register checks (_prevent_later_uses) don't see the condition, and skip a value
        already used once, which the condition's use is. So whatever in it matches is pinned to its temp; failing that, the
        condition stops m2c for this function.

        That includes a variable the delay slot assigns again (2026-11-07, `reassigned`): func_8014434C has `bne $s0, $v0, ...;
        addiu $s0, $s0, -1` with s0 already the variable var_s0, and m2c wrote `var_s0 -= 1; if (var_s0 == 4)` -- the value
        from after the delay slot, where the N64 compares the one from before. A variable read counted as settled (it's not
        a pending expression), so nothing was pinned and nothing failed. Now reading the variable being assigned is a match:
        what holds it is pinned to a temp if there is such a value (`temp = var_s0 + 1` made before the assignment), and a
        condition that reads it directly stops m2c with the error below, which makes lift.py run it again with
        --split-delay-slots (the delay slot copied into each way out, after the compare: flow_graph.py build_blocks)."""
        if self.branch_condition is not None and not pin_pending(self.branch_condition, expr_filter, reassigned=True):
            raise DecompFailure("A branch condition is changed by its delay slot")
        if self.switch_control is not None and not pin_pending(self.switch_control.control_expr, expr_filter, reassigned=True):
            raise DecompFailure("A switch value is changed by its delay slot")

    def set_initial_reg(self, reg: Register, expr: Expression, meta: RegMeta) -> None:
        assert meta.initial
        expr = self._eval_once(
            expr,
            emit_exactly_once=False,
            transparent=True,
            reg=reg,
            source=PrologueRef(reg),
        )
        self.regs.global_set_with_meta(reg, expr, meta)
        if expr.var.is_emitted:
            # For arguments only used as phis, we need to access them here in
            # order for the PassedInArg code path in __getitem__ to get hit.
            # (Arguably a bit hacky...)
            self.regs[reg]

    def set_reg(
        self,
        reg: Register,
        expr: Expression,
    ) -> Expression:
        if isinstance(expr, LocalVar):
            if (
                isinstance(self.node, ReturnNode)
                and self.stack_info.maybe_get_register_var(reg)
                and self.stack_info.in_callee_save_reg_region(expr.value)
                and reg in self.stack_info.callee_save_regs
            ):
                # Elide saved register restores with --reg-vars (it doesn't
                # matter in other cases).
                return expr
            if (
                self.stack_info.global_info.stack_spill_detection
                and expr in self.local_var_writes
            ):
                # Elide register restores (only for the same register for now,
                # to be conversative).
                orig_reg, orig_expr, force = self.local_var_writes[expr]
                # (No Mercy Arcade, 2026-11-11, trial 5, --register-cuts: not when the slot is narrower than a register. `sh $a1,
                #  0x256($sp)` then `lhu $a1, 0x256($sp)` is no save and restore: it cuts a1 to 16 bits. Putting the stored value
                #  back made the parameter itself u16 with no cast anywhere, and lift.py widens parameters to the register
                #  (to_register_width), so func_80039C20 compared and passed on all 32 bits of a1: 81,156 of 112,500 tests
                #  differed. Left alone, the register is the slot read at the slot's own size, as any other load of it is.)
                narrow_slot = (
                    self.stack_info.global_info.register_cuts
                    and (expr.type.get_size_bytes() or 4) < 4
                )
                if orig_reg == reg and not narrow_slot:
                    expr = orig_expr
                    if force:
                        base_expr = expr
                        while isinstance(base_expr, Cast):
                            base_expr = base_expr.expr
                        if isinstance(base_expr, EvalOnceExpr):
                            base_expr.force()

        return self.set_reg_real(reg, expr)

    def set_reg_real(
        self,
        reg: Register,
        uw_expr: Expression,
        *,
        transparent: Optional[bool] = None,
        emit_exactly_once: bool = False,
        function_return: bool = False,
    ) -> Expression:
        source = self.regs.current_instr_ref()

        # (No Mercy Arcade, 2026-11-07: a load comes after every call before it -- see pin_pending_calls.)
        loaded = uw_expr
        while isinstance(loaded, Cast):
            loaded = loaded.expr
        if isinstance(loaded, (StructAccess, ArrayAccess, LocalVar)):
            self.pin_pending_calls()

        if transparent is None:
            transparent = should_wrap_transparently(uw_expr)

        expr: RegExpression = self._eval_once(
            uw_expr,
            emit_exactly_once=emit_exactly_once,
            transparent=transparent,
            reg=reg,
            source=source,
        )

        if reg == Register("zero"):
            # Emit the expression as is. It's probably a volatile load.
            expr.use()
            self.write_statement(ExprStmt(expr))
        else:
            in_pattern = source.instruction.in_pattern
            self.regs.set_with_meta(
                reg,
                expr,
                RegMeta(in_pattern=in_pattern, function_return=function_return),
            )
        return expr

    def clear_caller_save_regs(self) -> None:
        for reg in self.stack_info.global_info.arch.temp_regs:
            if reg in self.regs:
                del self.regs[reg]

    def maybe_clear_local_var_writes(self, func_args: List[Expression]) -> None:
        # Clear the `local_var_writes` dict if any of the `func_args` contain
        # a reference to a stack var. (The called function may modify the stack,
        # replacing the value we have in `local_var_writes`.)
        for arg in func_args:
            if uses_expr(
                arg,
                lambda expr: isinstance(expr, AddressOf)
                and isinstance(expr.expr, LocalVar),
            ):
                self.local_var_writes.clear()
                return

    def set_branch_condition(self, cond: Condition) -> None:
        assert isinstance(self.node, ConditionalNode)
        assert self.branch_condition is None
        self.branch_condition = cond

    def set_switch_expr(self, expr: Expression, just_index: bool = False) -> None:
        assert isinstance(self.node, SwitchNode)
        assert self.switch_control is None
        if just_index:
            self.switch_control = SwitchControl(expr, num_cases=len(self.node.cases))
        else:
            self.switch_control = SwitchControl.from_expr(expr, len(self.node.cases))

    def write_statement(self, stmt: Statement) -> None:
        self.to_write.append(stmt)

    def store_memory(self, store: StoreStmt, reg: Register) -> None:
        source = store.source
        dest = store.dest
        if isinstance(dest, SubroutineArg):
            # About to call a subroutine with this argument. Skip arguments for the
            # first four stack slots; they are also passed in registers.
            if dest.value >= self.stack_info.global_info.arch.home_space_size:
                self.subroutine_args[dest.value] = source
                self.subroutine_arg_mnemonics[dest.value] = self.regs.current_instr().mnemonic
                # (--early-stack-args, 2026-11-11, scratch/different_overnight C2: always, so the slot holds it from where the
                #  original stores it. func_800F00C0 reads sp20[s4] with s4 = -3 after its sw $v0, 0x14($sp): the original saw
                #  that word, the converted one -- which stored it only at the call -- what was there before.)
                if not self._call_later_in_block() or self.stack_info.global_info.early_stack_args:
                    # (No Mercy Arcade: for a call in another block -- written to the frame slot. See FrameArgSlot.)
                    mnemonic = self.regs._current_instr_ref.instruction.mnemonic
                    # (sh and sb too, 2026-11-12, agent "refused": a halfword or byte stored in the outgoing area, written to
                    #  its slot with its own size -- func_80122FBC's `sh $v0, 0x2E($sp)`. Until then an error.)
                    ctype = STACK_ARG_CTYPES.get(mnemonic)
                    if ctype is None:
                        self.write_statement(
                            CommentStmt(f"Error: stack arg stored with {mnemonic}")
                        )
                        return
                    source.use()
                    self.prevent_later_function_calls()
                    self.pin_pending_reads()
                    self.write_statement(
                        StoreStmt(
                            source=as_type(source, Type.f32() if ctype == "f32" else Type.intish(), True),
                            dest=FrameArgSlot(dest.value, type=dest.type, ctype=ctype),
                        )
                    )
                    if self.stack_info.global_info.early_stack_args:
                        # (The call then reads the slot, M2C_STACK_ARG, as one in another block does: what the callee sees.)
                        self.subroutine_args.pop(dest.value, None)
            return

        raw_value = source
        while isinstance(raw_value, Cast) and raw_value.reinterpret:
            raw_value = raw_value.expr

        if isinstance(dest, LocalVar):
            self.stack_info.add_local_var(dest)
            # When preserving values on the stack across function calls,
            # ignore the type of the stack variable. The same stack slot
            # might be used to preserve values of different types.
            self.local_var_writes[dest] = (reg, raw_value, False)

        if (
            isinstance(dest, (LocalVar, PassedInArg))
            and early_unwrap(raw_value) == dest
        ):
            # Elide self assignments for stack locations. This commonly happens
            # for variables preserved across multiple function calls.
            return

        # Emit a write. This includes four steps:
        # - mark the expression as used (since writes are always emitted)
        # - mark the dest used (if it's a struct access it needs to be
        # evaluated, though ideally we would not mark the top-level expression
        # used; it may cause early emissions that shouldn't happen)
        # - mark other usages of the dest as "emit before this point if used".
        # - emit the actual write.
        #
        # Note that the prevent_later_value_uses step happens after use(), since
        # the stored expression is allowed to reference its destination var,
        # but before the write is written, since prevent_later_value_uses might
        # emit writes of its own that should go before this write. In practice
        # that probably never occurs -- all relevant register contents should be
        # EvalOnceExpr's that can be emitted at their point of creation, but
        # I'm not 100% certain that that's always the case and will remain so.
        source.use()
        dest.use()
        self.prevent_later_value_uses(dest)
        self.prevent_later_function_calls()
        # (No Mercy Arcade: every read still held anywhere gets its temp before the store, not just reads of the same place.
        # See pin_pending_reads.)
        self.pin_pending_reads()
        self.write_statement(store)

    def unaligned_load(self, reg: Register, call: Expression) -> None:
        """No Mercy Arcade, 2026-11-07 (--exact-unaligned, arch_mips.py UnalignedPairPattern): an unaligned read -- an lwl/lwr
        pair, or one of them alone -- written as a call to the converted code's helper, set into its register.

        It is pinned where the instruction is, `temp_v0 = lift_unaligned_load32(...)`, after every call before it (as a load is,
        pin_pending_calls): so it reads memory exactly when the original does -- after the stores and calls before it, and before
        the ones after it, which find it already written out."""
        self.pin_pending_calls()
        expr = self.set_reg_real(reg, call, emit_exactly_once=True, transparent=False)
        expr.force()

    def unaligned_store(self, call: Expression) -> None:
        """No Mercy Arcade, 2026-11-07 (--exact-unaligned): an unaligned write -- an swl/swr pair, or one of them alone -- written
        as a call to the converted code's helper, with what store_memory does before a store: every call before it, and every
        read still waiting in a register or a value, written out first (so none moves past the write). And m2c's note of what
        each stack slot was last stored from is forgotten: m2c reloads a register saved to a slot as the value it saved
        (set_reg's "register restores"), which would skip what this wrote there. The slot is then read from memory, as the
        original does."""
        call.use()
        self.pin_pending_calls()
        self.prevent_later_function_calls()
        self.pin_pending_reads()
        self.local_var_writes.clear()
        self.write_statement(ExprStmt(call))

    def _call_later_in_block(self) -> bool:
        """Whether a function call comes after the current instruction in this block (No Mercy Arcade, 2026-11-07)."""
        current = self.regs._current_instr_ref
        later = False
        for ref in self.node.block.instruction_refs:
            if later and ref.instruction.function_target is not None:
                return True
            if ref is current:
                later = True
        return False

    def push_subroutine_arg(self, source: Expression) -> None:
        self.subroutine_args = {
            offset + 4: arg for offset, arg in self.subroutine_args.items()
        }
        self.subroutine_args[0] = source

    def _reg_probably_meant_as_function_argument(
        self, reg: Register, call_instr: InstrRef
    ) -> bool:
        for source in self.stack_info.flow_graph.instr_inputs[call_instr].get(reg):
            uses = self.stack_info.flow_graph.instr_uses[source].get(reg)
            assert call_instr in uses
            if (
                len(uses) == 1
                and isinstance(source, InstrRef)
                and source.instruction.function_target is None
                and call_instr.instruction.function_target != reg
            ):
                return True
        return False

    def _call_passes(self, instr: Instruction, reg: Register) -> Optional[bool]:
        """No Mercy Arcade: whether the call `instr` is given `reg` as an argument by its declaration (in the C++ that
        argument is put in the register), or None if that isn't known: a call through a pointer, or a function declared
        without its parameters or with `...`."""
        target = instr.function_target
        if not isinstance(target, AsmGlobalSymbol):
            return None
        fn = as_function_ptr(self.stack_info.global_info.address_of_gsym(target.symbol_name))
        fn_sig = fn.type.get_function_pointer_signature()
        if fn_sig is None or not fn_sig.params_known or fn_sig.is_variadic:
            return None
        abi = self.stack_info.global_info.arch.function_abi(fn_sig, {}, for_call=True)
        return any(slot.loc.reg == reg for slot in abi.arg_slots)

    def _left_by_calls(self, reg: Register, call_instr: InstrRef) -> bool:
        """No Mercy Arcade, --left-registers: whether, on every way to `call_instr`, `reg` (a parameter register) was last
        changed by something the C++ does the same way -- so that the register there holds, in the C++, what the original
        passes in it. See LeftInRegister.

        Walking back from the call along every path, the first thing found that touches the register settles that path:
          - a call given the register as an argument: fine (the C++ put the same value there, and the call ran on it);
          - a call not given it: the register is what that callee left from what was there before it, so the walk goes on
            past it (the C++ runs the same translated callee);
          - any other instruction that writes it, or changes it as a side effect (one of m2c's folded patterns): not
            fine -- the function's own instructions don't go into the C++'s registers;
          - the start of the function with nothing found: fine -- the C++ was handed the N64's registers as they were.
        Loops and joins are followed through every predecessor; a block is walked once."""
        node = self.node
        refs = list(node.block.instruction_refs)
        index = next((i for i, r in enumerate(refs) if r is call_instr), None)
        if index is None:
            return False
        work: List[Tuple[Node, List[InstrRef]]] = [(node, refs[:index])]
        seen: Set[Node] = set()
        while work:
            n, todo = work.pop()
            settled = False
            for ref in reversed(todo):
                instr = ref.instruction
                if instr.function_target is not None and (reg in instr.clobbers or reg in instr.outputs):
                    passes = self._call_passes(instr, reg)
                    if passes is None:
                        return False
                    if passes:
                        settled = True
                        break
                    continue
                if reg in instr.outputs or reg in instr.clobbers:
                    return False
            if settled:
                continue
            # (A block with no predecessors is the start of the function: what the caller had there is fine. The entry
            # block can also be reached again through a loop, so its predecessors, if any, are followed like any other's.)
            for p in n.parents:
                if p not in seen:
                    seen.add(p)
                    work.append((p, list(p.block.instruction_refs)))
        return True

    def make_function_call(
        self, fn_target: Expression, outputs: List[Location]
    ) -> None:
        call_instr = self.regs.current_instr_ref()
        arch = self.stack_info.global_info.arch
        fn_target = as_function_ptr(fn_target)
        fn_sig = fn_target.type.get_function_pointer_signature()
        assert fn_sig is not None, "known function pointers must have a signature"

        instr_inputs = self.stack_info.flow_graph.instr_inputs[call_instr]
        likely_regs: Dict[Register, bool] = {}
        for reg, data in self.regs.contents.items():
            # We use a much stricter filter for PPC than MIPS, because the same
            # registers can be used arguments & return values.
            # The ABI can also mix & match the rN & fN registers, which makes the
            # "require" heuristic less powerful.
            # The heuristic is as follows:
            #
            # - function returns are never treated as likely arguments
            # - other values set in the same basic block *are* treated as likely
            #   arguments
            # - other values set in a different basic block are not treated as
            #   likely arguments if they are also read by another instruction
            #
            # We don't do this stricter filtering for variadic functions, though, since
            # those don't provide "fix the context" as a way out if we get it wrong.
            #
            # For ARM we have the same filter, except with the second clause removed.
            # This is because Thumb has high register pressure, so there are even
            # more false positives.
            #
            # Implementation note: the `meta.function_return` bit is only accurate for
            # registers set within this basic block, because `propagate_register_meta`
            # has not yet been called. However, it's only in that case we read it.
            if reg not in arch.argument_regs:
                likely_regs[reg] = False
            elif (
                arch.arch in (Target.ArchEnum.PPC, Target.ArchEnum.ARM)
                and not fn_sig.is_variadic
                and (
                    data.meta.inherited
                    or data.meta.function_return
                    or arch.arch == Target.ArchEnum.ARM
                )
                and not self._reg_probably_meant_as_function_argument(reg, call_instr)
            ):
                likely_regs[reg] = False
            elif all(
                not isinstance(source, InstrRef) or source.instruction.in_pattern
                for source in instr_inputs.get(reg)
            ):
                likely_regs[reg] = False
            else:
                likely_regs[reg] = True

        abi = arch.function_abi(fn_sig, likely_regs, for_call=True)

        func_args: List[Expression] = []
        for slot in abi.arg_slots:
            if slot.loc.reg:
                if (
                    self.stack_info.global_info.left_registers
                    and fn_sig.params_known
                    and slot.loc.reg not in self.regs
                    and slot.loc.reg in arch.argument_regs
                    and not slot.loc.reg.is_float()
                    and self._left_by_calls(slot.loc.reg, call_instr)
                ):
                    # (No Mercy Arcade: a parameter register m2c has no value for, which the C++ already holds as the
                    # original passes it -- see LeftInRegister.)
                    expr = LeftInRegister(slot.loc.reg, type=slot.type)
                else:
                    expr = self.regs[slot.loc.reg]
            else:
                offset = slot.loc.offset
                assert offset is not None
                if offset in self.subroutine_args:
                    expr = self.subroutine_args.pop(offset)
                else:
                    # (No Mercy Arcade: read from the frame slot, where the callee reads it. See FrameArgSlot.)
                    expr = FrameArgSlot(offset, type=Type.intish())
            func_args.append(
                CommentExpr.wrap(as_type(expr, slot.type, True), prefix=slot.comment)
            )

        for slot in abi.possible_slots:
            assert slot.loc.reg is not None
            func_args.append(self.regs[slot.loc.reg])

        # Add the arguments after a3.
        # TODO: limit this based on abi.arg_slots. If the function type is known
        # and not variadic, this list should be empty.
        for offset, arg in sorted(self.subroutine_args.items()):
            if fn_sig.params_known and not fn_sig.is_variadic:
                # No Mercy Arcade, 2026-11-07: a word the caller stores in its stack argument area, past the parameters the
                # declaration gives (lift.py's prototype counts the stack words a function reads at fixed offsets, and a
                # sprintf-like one, func_8000A3B8, reads its list through a pointer instead). m2c added it to the call as an
                # argument marked "extra?" -- its guess that it might be one -- which put it in the wrong place: after the
                # declared ones, so the word stored at sp+0x10 became the second argument (a1) of a one-parameter call,
                # where the callee reads it at sp+0x10. What the original does is a store to that frame slot, so that's
                # what's written now, before the call, with the instruction's own kind (sw, or swc1 for a float), the
                # same way as a stack argument stored in another block (FrameArgSlot). Whether the callee reads it or not,
                # the frame then holds what the original's does.
                mnemonic = self.subroutine_arg_mnemonics.get(offset)
                ctype = STACK_ARG_CTYPES.get(mnemonic or "")
                if ctype is None:
                    func_args.append(CommentExpr.wrap(arg, prefix="extra?"))
                    continue
                # (As a store does in store_memory: what's still waiting to be written out -- an earlier call, a read --
                # goes before it.)
                arg.use()
                self.prevent_later_function_calls()
                self.pin_pending_reads()
                self.write_statement(
                    StoreStmt(
                        source=as_type(arg, Type.f32() if ctype == "f32" else Type.intish(), True),
                        dest=FrameArgSlot(offset, type=Type.any_reg(), ctype=ctype),
                    )
                )
            else:
                func_args.append(arg)

        if not fn_sig.params_known:
            while len(func_args) > len(fn_sig.params):
                fn_sig.params.append(FunctionParam())
            # When the function signature isn't provided, the we only assume that each
            # parameter is "simple" (<=4 bytes, no return struct, etc.). This may not
            # match the actual function signature, but it's the best we can do.
            # Without that assumption, the logic from `function_abi` would be needed here.
            for i, (arg_expr, param) in enumerate(zip(func_args, fn_sig.params)):
                func_args[i] = as_type(arg_expr, param.type.decay(), True)

        # Reset subroutine_args, for the next potential function call.
        self.subroutine_args.clear()
        self.subroutine_arg_mnemonics.clear()

        source = self.regs.current_instr_ref()
        call: Expression = FuncCall(
            fn_target, func_args, fn_sig.return_type.weaken_void_ptr()
        )
        call = self._eval_once(
            call,
            emit_exactly_once=True,
            transparent=False,
            reg=Register("ret"),
            source=source,
        )

        # Clear out caller-save registers, for clarity and to ensure that
        # argument regs don't get passed into the next function.
        self.clear_caller_save_regs()

        # Clear out local var write tracking if any argument contains a stack
        # reference. That dict is used to track register saves/restores, which
        # are unreliable if we call a function with a stack reference.
        self.maybe_clear_local_var_writes(func_args)

        # Prevent reads and function calls from moving across this call.
        # This isn't really right, because this call might be moved later,
        # and then this prevention should also be... but it's the best we
        # can do with the current code architecture.
        self.prevent_later_function_calls()
        self.prevent_later_reads()
        # (No Mercy Arcade: and every read still held -- in a saved register, a value kept across calls, the branch condition --
        # gets its temp before the call. See pin_pending_reads.)
        self.pin_pending_reads()

        return_reg_vals = arch.function_return(call)
        for out in outputs:
            if not isinstance(out, Register):
                continue
            val = return_reg_vals[out]
            self.set_reg_real(out, val, transparent=False, function_return=True)

        self.has_function_call = True


def evaluate_instruction(instr_ref: InstrRef, state: NodeState) -> None:
    instr = instr_ref.instruction
    # Check that instr's attributes are consistent
    if instr.is_return:
        assert isinstance(state.node, ReturnNode)
    if instr.is_conditional:
        assert state.branch_condition is None and state.switch_control is None

    if instr.eval_fn is not None:
        args = InstrArgs(instr_ref, instr.args, state.regs, state.stack_info)
        eval_fn = typing.cast(Callable[[NodeState, InstrArgs], object], instr.eval_fn)
        eval_fn(state, args)

    # Check that conditional instructions set at least one of branch_condition or switch_control
    if instr.is_conditional:
        assert state.branch_condition is not None or state.switch_control is not None


def translate_node_body(state: NodeState) -> BlockInfo:
    """
    Given a node and current register contents, return a BlockInfo containing
    the translated AST for that node.
    """
    for instr_ref in state.node.block.instruction_refs:
        with state.regs.set_current_instr(instr_ref):
            evaluate_instruction(instr_ref, state)

    if state.branch_condition is not None:
        state.branch_condition.use()
    if state.switch_control is not None:
        state.switch_control.control_expr.use()

    return BlockInfo(
        to_write=state.to_write,
        return_value=None,
        switch_control=state.switch_control,
        branch_condition=state.branch_condition,
        final_register_states=state.regs,
        has_function_call=state.has_function_call,
    )


def translate_block(
    state: NodeState,
    options: Options,
) -> BlockInfo:
    """
    Given a FlowGraph node and current symbolic execution state, symbolically
    execute all instructions in the block, resulting in updated execution state
    as well as a BlockInfo with C statements and if conditions to emit for the
    given node.
    """

    node = state.node
    if options.debug:
        print(f"\nNode in question: {node}")

    # Translate the given node and discover final register states.
    try:
        block_info = translate_node_body(state)
        if options.debug:
            print(block_info)
    except Exception as e:  # TODO: handle issues better
        if options.stop_on_error:
            raise

        instr: Optional[Instruction] = None
        if isinstance(e, InstrProcessingFailure) and isinstance(e.__cause__, Exception):
            instr = e.instr
            e = e.__cause__

        if isinstance(e, DecompFailure):
            emsg = str(e)
            print(emsg)
        else:
            tb = e.__traceback__
            traceback.print_exception(None, e, tb)
            emsg = str(e) or traceback.format_tb(tb)[-1]
            emsg = emsg.strip().split("\n")[-1].strip()

        error_stmts: List[Statement] = [CommentStmt(f"Error: {emsg}")]
        if instr is not None:
            print(
                f"Error occurred while processing instruction: {instr}", file=sys.stderr
            )
            error_stmts.append(CommentStmt(f"At instruction: {instr}"))
        print(file=sys.stderr)
        block_info = BlockInfo(
            to_write=error_stmts,
            return_value=None,
            switch_control=None,
            branch_condition=ErrorExpr(),
            final_register_states=state.regs,
            has_function_call=False,
        )

    return block_info


def create_dominated_node_state(
    parent_state: NodeState,
    child: Node,
    used_naive_phis: List[NaivePhiExpr],
) -> NodeState:
    """
    Create a NodeState for translating a child of a node in the dominator tree,
    generating phi nodes for register contents that cannot be inherited directly.
    """
    stack_info = parent_state.stack_info
    new_regs = RegInfo(stack_info=stack_info)
    child_state = NodeState(node=child, regs=new_regs, stack_info=stack_info)
    # (2026-11-11, trial 5: what was stale where the parent block ended is stale here -- is_stale. The marks made while this
    #  state is set up, below, go into its own.)
    child_state.stale = dict(parent_state.stale)
    if child.loop is not None:
        # (No Mercy Arcade, 2026-11-10) Going into a loop: a memory read still held in a register gets its temp now, before the
        # loop, where the original read it. m2c prints a value used once where it is used, and one use written inside a loop
        # runs every time round -- so a load the original made once became a load per pass. func_801099A4 loads a double
        # before its loop (`ldc1 $f4`) and stores through a pointer inside it; with enough passes the stores reach that
        # double, and the C++ read the changed one (4,308 of 59,740 tests DIFFERENT). func_80102120 loads `lh $a3, 0xA0($a0)`
        # before a loop that touches no memory at all; the C++ read it on every pass and ran past the tester's limit on
        # memory accesses where the original has none to count (ran-long). pin_pending_reads doesn't catch these: the store
        # (if there is one) comes after the use in the text, and only comes before it the next time round.
        def reads_jump_table(e: Expression) -> bool:
            # (2026-11-10, after 0.75.0) The entry a switch is about to jump through: the switch makes that read itself, so it
            # isn't pinned. Pinned, it was written as its own statement naming the table (`&jtbl_80103310 + i * 4`), which the
            # converted code has no declaration for: func_800E6EBC and func_80141034 stopped compiling, whose loop is a case.
            if isinstance(e, GlobalSymbol) and e.c_symbol_name.startswith("jtbl_"):
                return True
            if isinstance(e, EvalOnceExpr):         # (its dependencies() refuses to be asked: look inside, as reads_phi_variable does)
                return reads_jump_table(e.wrapped_expr)
            return any(reads_jump_table(d) for d in e.dependencies())

        def is_read(e: Expression) -> bool:
            return isinstance(e, (LocalVar, StructAccess, ArrayAccess)) and not reads_jump_table(e)
        for data in parent_state.regs.contents.values():
            pin_pending(data.value, is_read)
    for reg, data in parent_state.regs.contents.items():
        new_regs.global_set_with_meta(
            reg,
            data.value,
            RegMeta(inherited=True, force=data.meta.force, initial=data.meta.initial),
        )

    phi_regs = (
        r for r in locs_clobbered_until_dominator(child) if isinstance(r, Register)
    )
    for reg in phi_regs:
        sources, uses_dominator = reg_sources(child, reg)

        inherited_phi = stack_info.get_planned_inherited_phi(child, reg)
        if inherited_phi is not None:
            if inherited_phi == PhiInheritSource.DOMINATOR:
                # A previous translation pass has determined that despite register
                # writes along the way to the dominator, using the dominator's
                # value is fine (because all possible phi values are the same).
                # Do reset the 'initial' meta bit though: the non-dominator sources
                # are non-initial (making the value more likely to be a function
                # argument).
                # TODO: it would be nice to keep doing type unification in this case.
                meta = new_regs.get_meta(reg)
                if meta is not None:
                    new_regs.update_meta(reg, replace(meta, initial=False))
                    continue
            else:
                # Same thing, except we are allowed to inherit the value from an
                # arbitrary parent, and we are guaranteed that it is trivial.
                # This can sometimes fail, either because the translation order is
                # such that no parents have yet been translated (this can happen
                # with diamond graphs that don't follow asm order; it's fixable but
                # not worth the complexity given the rarity), or because the parent
                # value is suddenly a NaivePhiExpr during this next translation
                # phase instead of the expected EvalOnceExpr (it's not clear how
                # this can happen but empirically it can).
                found_par_value: Optional[EvalOnceExpr] = None
                for parent in child.parents:
                    if parent.block.block_info:
                        par_regs = get_block_info(parent).final_register_states
                        par_value = par_regs.contents[reg].value
                        if isinstance(par_value, EvalOnceExpr) and par_value.trivial:
                            found_par_value = par_value
                            break

                if found_par_value is not None:
                    new_val = EvalOnceExpr(
                        wrapped_expr=found_par_value.wrapped_expr,
                        var=Var(
                            stack_info,
                            "_m2c_inherited_phi",
                            is_planned=False,
                            type=found_par_value.type,
                        ),
                        type=found_par_value.type,
                        sources=sources,
                        emit_exactly_once=False,
                        trivial=True,
                        transparent=True,
                        is_fictive_temp=False,
                    )
                    new_regs.global_set_with_meta(reg, new_val, RegMeta(inherited=True))
                    continue

        if uses_dominator:
            dom_expr = parent_state.regs.get_raw(reg)
            if dom_expr is None:
                sources = []
            elif isinstance(dom_expr, (EvalOnceExpr, PlannedPhiExpr, NaivePhiExpr)):
                sources.extend(dom_expr.sources)
            else:
                assert_never(dom_expr)

        if sources:
            # For each source of this phi, there will be an associated EvalOnceExpr.
            # If there are planned vars associated with those EvalOnceExpr's --
            # assigning them pre-determined vars and forcing the corresponding
            # EvalOnceStmt's to be emitted -- *and* those planned vars are all the
            # same, we can emit a reference to that var as our phi.
            #
            # Otherwise, we emit a naive phi expression, which as part of
            # `assign_naive_phis` will either do "all sources values are the
            # same"-based deduplication, or emit a planned var for the next
            # translation pass. See the doc comment for NaivePhiExpr for more
            # information.
            expr: Optional[RegExpression]
            var = stack_info.get_planned_var(reg, sources[0])
            if var is not None and all(
                stack_info.get_planned_var(reg, s) == var for s in sources[1:]
            ):
                expr = PlannedPhiExpr(var, var.type, sources)
            else:
                expr = NaivePhiExpr(
                    reg=reg,
                    node=child,
                    type=Type.any_reg(),
                    sources=sources,
                    uses_dominator=uses_dominator,
                    used_naive_phis=used_naive_phis,
                )
            new_regs.global_set_with_meta(reg, expr, RegMeta(inherited=True))

        elif reg in new_regs:
            # Along some path to the dominator the register is clobbered by a
            # function call. Mark it as undefined. (This helps return value and
            # function call argument heuristics.)
            del new_regs[reg]

    # If we inherit an expression from the dominator that mentions a var,
    # and that var gets assigned to somewhere along a path to the dominator,
    # using that expression requires it to be made into a temp.
    clobbered_vars, has_fn_call = find_clobbers_until_dominator(stack_info, child)
    child_state.prevent_later_var_uses(clobbered_vars)

    # Prevent function calls from being moved across basic blocks, except for
    # trivial return stubs.
    if len(child.parents) != 1 or len(parent_state.node.children()) != 1:
        child_state.prevent_later_function_calls()

    # Prevent reads if there are function calls on the path to the dominator.
    # TODO: we should also prevent reads if there are overlapping writes,
    # matching the prevent_later_value_uses call in `store_memory`, but that's
    # much harder, because store targets are only available during actual
    # translation.
    if has_fn_call:
        child_state.prevent_later_reads()

    return child_state


def translate_all_blocks(
    initial_state: NodeState,
    used_naive_phis: List[NaivePhiExpr],
    return_blocks: List[BlockInfo],
    options: Options,
) -> None:
    """
    Do translation for all non-terminal nodes, attaching block info to them.

    Translation is performed in order of the dominator tree. Each node inherits
    register state from its immediate dominator (the closest parent node that
    control flow is guaranteed to pass through), with some register contents
    being replaced by phi nodes if they may be changed along the path to the
    dominator.
    """
    # Do the traversal non-recursively, in order to get nicer stacks for profiling.
    translate_stack: List[Tuple[Optional[NodeState], Node]] = [
        (None, initial_state.node)
    ]
    while translate_stack:
        parent_state, node = translate_stack.pop()
        if parent_state is None:
            state = initial_state
        else:
            state = create_dominated_node_state(parent_state, node, used_naive_phis)
        # (No Mercy Arcade, 2026-11-11, trial 5: this block's stale values are the ones early_unwrap goes by -- is_stale.)
        global stale_here
        stale_here = state.stale
        try:
            block_info = translate_block(state, options)
        finally:
            stale_here = None
        node = state.node
        node.block.add_block_info(block_info)
        if isinstance(node, ReturnNode):
            return_blocks.append(block_info)
        # Add children to the translation queue in reverse order, to get a normally
        # ordered pre-order traversal. (Doesn't affect the result majorly but it's
        # easier to think about.)
        for child in reversed(state.node.immediately_dominates):
            if not isinstance(child, TerminalNode):
                translate_stack.append((state, child))


def resolve_types_late(stack_info: StackInfo) -> None:
    """
    After translating a function, perform a final type-resolution pass.
    """
    # Final check over stack var types. Because of delayed type unification, some
    # locations should now be marked as "weak".
    for location in stack_info.weak_stack_var_types.keys():
        stack_info.get_stack_var(location, store=False)

    # Use dereferences to determine pointer types
    struct_type_map = stack_info.get_struct_type_map()
    for var, offset_type_map in struct_type_map.items():
        if len(offset_type_map) == 1 and 0 in offset_type_map:
            # var was probably a plain pointer, not a struct
            # Try to unify it with the appropriate pointer type,
            # to fill in the type if it does not already have one
            type = offset_type_map[0]
            var.type.unify(Type.ptr(type))


@dataclass
class FunctionInfo:
    stack_info: StackInfo
    flow_graph: FlowGraph
    return_type: Type
    symbol: GlobalSymbol


@dataclass
class FailedToGenerateInitializer(Exception):
    reason: str


@dataclass
class GlobalInfo:
    asm_data: AsmData
    arch: Arch
    target: Target
    local_functions: Set[str]
    typemap: TypeMap
    typepool: TypePool
    deterministic_vars: bool
    stack_spill_detection: bool
    register_cuts: bool = False
    access_types: bool = False
    early_stack_args: bool = False
    left_registers: bool = False
    global_symbol_map: Dict[str, GlobalSymbol] = field(default_factory=dict)
    persistent_function_state: Dict[str, PersistentFunctionState] = field(
        default_factory=lambda: defaultdict(PersistentFunctionState)
    )

    def get_persistent_function_state(self, func_name: str) -> PersistentFunctionState:
        return self.persistent_function_state[func_name]

    def asm_data_value(self, sym_name: str) -> Optional[AsmDataEntry]:
        return self.asm_data.values.get(sym_name)

    def address_of_gsym(self, sym_name: str) -> AddressOf:
        if sym_name in self.global_symbol_map:
            sym = self.global_symbol_map[sym_name]
        else:
            c_sym_name = self.arch.c_symbol_name(sym_name)
            demangled_symbol: Optional[CxxSymbol] = None
            demangled_str: Optional[str] = None
            if (
                self.target.language == Target.LanguageEnum.CXX
                and self.target.compiler == Target.CompilerEnum.MWCC
            ):
                try:
                    demangled_symbol = demangle_codewarrior_parse(sym_name)
                except ValueError:
                    pass
                else:
                    demangled_str = str(demangled_symbol)

            sym = self.global_symbol_map[sym_name] = GlobalSymbol(
                c_symbol_name=c_sym_name,
                type=Type.any(),
                asm_data_entry=self.asm_data_value(sym_name),
                demangled_str=demangled_str,
            )

            # If the symbol is a C++ vtable, try to build a custom type for it by parsing it
            if (
                self.target.language == Target.LanguageEnum.CXX
                and self.target.compiler == Target.CompilerEnum.MWCC
                and sym_name.startswith("__vt__")
                and sym.asm_data_entry is not None
            ):
                sym.type.unify(self.vtable_type(sym_name, sym.asm_data_entry))

            fn = self.typemap.functions.get(c_sym_name)
            ctype: Optional[CType]
            if fn is not None:
                ctype = fn.type
            else:
                ctype = self.typemap.var_types.get(c_sym_name)

            if ctype is not None:
                sym.symbol_in_context = True
                sym.initializer_in_typemap = (
                    c_sym_name in self.typemap.vars_with_initializers
                )
                if self.typepool.unk_inference and is_unk_type(ctype, self.typemap):
                    type = Type.gsym_unk_ctype(ctype, self.typemap, self.typepool)
                else:
                    type = Type.ctype(ctype, self.arch, self.typemap, self.typepool)
                sym.type.unify(type)
                if c_sym_name not in self.typepool.unknown_decls:
                    sym.type_provided = True
            elif sym_name in self.local_functions:
                sym.type.unify(Type.function())

            # Do this after unifying the type in the typemap, so that it has lower precedence
            if demangled_symbol is not None:
                sym.type.unify(
                    Type.demangled_symbol(
                        self.arch, self.typemap, self.typepool, demangled_symbol
                    )
                )

        return AddressOf(sym, type=sym.type.reference())

    def vtable_type(self, sym_name: str, asm_data_entry: AsmDataEntry) -> Type:
        """
        Parse MWCC vtable data to create a custom struct to represent it.
        This format is not well documented, but is briefly explored in this series of posts:
        https://web.archive.org/web/20220413174849/http://hacksoflife.blogspot.com/2007/02/c-objects-part-2-single-inheritance.html
        """
        size = asm_data_entry.size_range_bytes()[1]
        struct = StructDeclaration.unknown(
            self.typepool, size=size, align=4, tag_name=sym_name
        )
        offset = 0
        for entry in asm_data_entry.data:
            if isinstance(entry, bytes):
                # MWCC vtables start with a pointer to a typeid struct (or NULL) and an offset
                if len(entry) % 4 != 0:
                    raise DecompFailure(
                        f"Unable to parse misaligned vtable data in {sym_name}"
                    )
                for i in range(len(entry) // 4):
                    field_name = f"{struct.new_field_prefix}{offset:X}"
                    struct.try_add_field(
                        Type.reg32(likely_float=False), offset, field_name, size=4
                    )
                    offset += 4
            else:
                sym = entry.as_symbol_without_addend()
                if sym is None:
                    raise DecompFailure(f"Unable to parse vtable data in {sym_name}")
                entry_name = sym
                try:
                    demangled_field_sym = demangle_codewarrior_parse(sym)
                    if demangled_field_sym.name.qualified_name is not None:
                        entry_name = str(demangled_field_sym.name.qualified_name[-1])
                except ValueError:
                    pass

                field = struct.try_add_field(
                    self.address_of_gsym(sym).type,
                    offset,
                    name=entry_name,
                    size=4,
                )
                assert field is not None
                field.known = True
                offset += 4
        return Type.struct(struct)

    def is_function_known_void(self, sym_name: str) -> bool:
        """Return True if the function exists in the context, and has no return value"""
        fn = self.typemap.functions.get(sym_name)
        if fn is None:
            return False
        return fn.ret_type is None

    def initializer_for_symbol(self, sym: GlobalSymbol, fmt: Formatter) -> str:
        assert sym.asm_data_entry is not None
        data = sym.asm_data_entry.data[:]

        def read_uint(n: int) -> int:
            """Read the next `n` bytes from `data` as an (long) integer"""
            assert 0 < n <= 16
            if not data:
                raise FailedToGenerateInitializer("not enough data")
            if not isinstance(data[0], bytes):
                arg = data[0].data
                raise FailedToGenerateInitializer(f"cannot parse {arg} as integer")
            if len(data[0]) < n:
                if len(data) > 1:
                    sym = data[1]
                    assert isinstance(sym, AsmSymbolicData)
                    raise FailedToGenerateInitializer(f"partial read of {sym.data}")
                else:
                    raise FailedToGenerateInitializer("not enough data")
            bs = data[0][:n]
            data[0] = data[0][n:]
            if not data[0]:
                del data[0]
            value = 0
            if not self.target.is_big_endian():
                bs = bs[::-1]
            for b in bs:
                value = (value << 8) | b
            return value

        def try_read_pointer() -> Optional[Expression]:
            """Read the next label from `data`"""
            if not data or isinstance(data[0], bytes):
                return None
            label = data[0].as_symbol_without_addend()
            if label is None:
                return None

            data.pop(0)
            expr = self.address_of_gsym(label)
            expr.use()
            return expr

        def for_type(type: Type) -> str:
            """Return the initializer for a single element of type `type`"""
            if type.is_struct() or type.is_array():
                struct_fields = type.get_initializer_fields()
                if not struct_fields:
                    if type.is_array():
                        raise FailedToGenerateInitializer("unsized array")
                    else:
                        raise FailedToGenerateInitializer("confusing struct layout")
                members = []
                for field in struct_fields:
                    if isinstance(field, int):
                        # Check that all padding bytes are 0
                        for i in range(field):
                            padding = read_uint(1)
                            if padding != 0:
                                raise FailedToGenerateInitializer("non-zero padding")
                    else:
                        members.append(for_type(field))
                return fmt.format_array(members)

            if type.is_reg():
                size = type.get_size_bytes()
                if not size:
                    raise FailedToGenerateInitializer("unknown size")

                if size == 4:
                    ptr = try_read_pointer()
                    if ptr is not None:
                        return as_type(ptr, type, silent=True).format(fmt)

                value = read_uint(size)
                enum_name = type.get_enum_name(value)
                if enum_name is not None:
                    return enum_name
                expr = as_type(Literal(value), type, True)
                return elide_literal_casts(expr).format(fmt)

            if type.get_size_bits() is None:
                raise FailedToGenerateInitializer("unknown type")

            # Type kinds K_FN and K_VOID do not have initializers
            raise FailedToGenerateInitializer("bad type")

        type = sym.type.get_array()[0]
        if sym.is_string_constant() and type is not None and type.get_size_bytes() == 1:
            return sym.format_string_constant(fmt)
        else:
            return for_type(sym.type)

    def find_forward_declares_needed(self, functions: List[FunctionInfo]) -> Set[str]:
        funcs_seen = set()
        forward_declares_needed = self.asm_data.mentioned_labels

        for func in functions:
            funcs_seen.add(func.stack_info.function.name)

            for instr in func.stack_info.function.body:
                if not isinstance(instr, Instruction):
                    continue

                for arg in instr.args:
                    if isinstance(arg, AsmGlobalSymbol):
                        func_name = arg.symbol_name
                    elif isinstance(arg, Macro) and isinstance(
                        arg.argument, AsmGlobalSymbol
                    ):
                        func_name = arg.argument.symbol_name
                    else:
                        continue

                    if func_name in self.local_functions:
                        if func_name not in funcs_seen:
                            forward_declares_needed.add(func_name)

        return forward_declares_needed

    def global_decls(
        self,
        fmt: Formatter,
        decls: Options.GlobalDeclsEnum,
        functions: List[FunctionInfo],
    ) -> str:
        # Format labels from symbol_type_map into global declarations.
        # As the initializers are formatted, this may cause more symbols
        # to be added to the global_symbol_map.
        forward_declares_needed = self.find_forward_declares_needed(functions)

        lines = []
        processed_names: Set[str] = set()
        while True:
            names: Set[str] = {
                n for n, s in self.global_symbol_map.items() if s.is_referenced
            }
            if decls == Options.GlobalDeclsEnum.ALL:
                for name, ent in self.asm_data.values.items():
                    if not ent.is_text:
                        names.add(name)
            names -= processed_names
            if not names:
                break
            for name in sorted(names):
                processed_names.add(name)
                sym = self.address_of_gsym(name).expr
                assert isinstance(sym, GlobalSymbol)
                data_entry = sym.asm_data_entry

                if data_entry is not None and data_entry.is_text:
                    if not sym.type.is_function():
                        continue
                    data_entry = None

                # Is the label defined in this unit (in the active AsmData file(s))
                is_in_file = data_entry is not None or name in self.local_functions
                # Is the label externally visible (mentioned in the context file)
                is_global = sym.symbol_in_context
                # Is the label a symbol in .rodata?
                is_const = data_entry is not None and data_entry.is_readonly

                if data_entry and data_entry.is_jtbl:
                    # Skip jump tables
                    continue
                if is_in_file and is_global and sym.type.is_function():
                    # Skip externally-declared functions that are defined here
                    continue
                if self.local_functions == {name}:
                    # Skip the function being decompiled if just a single one
                    continue
                if not is_in_file and sym.type_provided:
                    # Skip externally-declared symbols that are defined in other files
                    continue

                # Flip the bss order if the corresponding option is set
                if data_entry:
                    file, index = data_entry.sort_order
                    data_entry_order = (
                        (file, index * -1)
                        if data_entry.is_bss and fmt.backwards_bss
                        else (file, index)
                    )
                else:
                    data_entry_order = ("", 0)

                sort_order = (
                    not sym.type.is_function(),
                    is_in_file,
                    is_global if not is_in_file else False,
                    is_const,
                    data_entry_order,
                    name,
                )
                qualifier = ""
                value: Optional[str] = None
                comments = []

                # Determine type qualifier: static, extern, or neither
                if is_in_file and is_global:
                    qualifier = ""
                elif is_in_file:
                    qualifier = "static"
                else:
                    qualifier = "extern"

                if sym.type.is_function():
                    comments.append(qualifier)
                    qualifier = ""

                # Try to guess if the symbol is an array (and if it is, its dimension) if
                # we have a data entry for it, and the symbol is either not in the typemap
                # or was a variable-length array there ("VLA", e.g. `int []`)
                # (Otherwise, if the dim is provided by the typemap, we trust it.)
                element_type, array_dim = sym.type.get_array()
                is_vla = element_type is not None and (
                    array_dim is None or array_dim <= 0
                )
                if data_entry and (not sym.type_provided or is_vla):
                    # The size of the data entry is uncertain, because of padding
                    # between sections. Generally `(max_data_size - data_size) < 16`.
                    min_data_size, max_data_size = data_entry.size_range_bytes()
                    # The size of the element type (not the size of the array type)
                    if element_type is None:
                        element_type = sym.type

                    # If we don't know the type, we can't guess the array_dim
                    type_size = element_type.get_size_bytes()
                    if type_size:
                        potential_dim, extra_bytes = sym.potential_array_dim(type_size)
                        if potential_dim == 0 and extra_bytes > 0:
                            # The type is too big for our data. (not an array)
                            comments.append(
                                f"type too large by {fmt.format_int(type_size - extra_bytes)}"
                            )
                        elif potential_dim > 1 or is_vla:
                            # NB: In general, replacing the types of Expressions can be sketchy.
                            # However, the GlobalSymbol here came from address_of_gsym(), which
                            # always returns a reference to the element_type.
                            array_dim = potential_dim
                            sym.type = Type.array(element_type, array_dim)

                        if potential_dim != 0 and extra_bytes > 0:
                            comments.append(
                                f"extra bytes: {fmt.format_int(extra_bytes)}"
                            )

                # Try to convert the data from .data/.rodata into an initializer
                if data_entry and not data_entry.is_bss and not data_entry.is_text:
                    try:
                        value = self.initializer_for_symbol(sym, fmt)
                    except FailedToGenerateInitializer as e:
                        # This warning helps distinguish .bss symbols from .data/.rodata,
                        # IDO only puts symbols in .bss if they don't have any initializer
                        comments.append(f"unable to generate initializer: {e.reason}")

                if is_const:
                    comments.append("const")

                    # Float & string constants are almost always inlined and can be omitted
                    if sym.is_string_constant():
                        continue
                    if (
                        array_dim is None
                        and data_entry is not None
                        and data_entry.used_as_literal
                    ):
                        continue

                # In "none" mode, do not emit any decls
                if decls == Options.GlobalDeclsEnum.NONE:
                    continue
                # In modes except "all", skip the decl if the context file already had an initializer
                if decls != Options.GlobalDeclsEnum.ALL and sym.initializer_in_typemap:
                    continue
                # In modes except "all", skip vtable decls when compiling C++
                if (
                    decls != Options.GlobalDeclsEnum.ALL
                    and self.target.language == Target.LanguageEnum.CXX
                    and self.target.compiler == Target.CompilerEnum.MWCC
                    and name.startswith("__vt__")
                ):
                    continue

                if (
                    sym.type.is_function()
                    and decls != Options.GlobalDeclsEnum.ALL
                    and name in self.local_functions
                    and name not in forward_declares_needed
                ):
                    continue

                qualifier = f"{qualifier} " if qualifier else ""
                value = f" = {value}" if value else ""
                lines.append(
                    (
                        sort_order,
                        fmt.with_comments(
                            f"{qualifier}{sym.type.to_decl(name, fmt)}{value};",
                            comments,
                        )
                        + "\n",
                    )
                )
        lines.sort()
        return "".join(line for _, line in lines)


def narrow_func_call_outputs(
    function: Function,
    global_info: GlobalInfo,
) -> None:
    """
    Modify the `outputs` list of function call Instructions using the context file.
    For now, this only handles known-void functions, but in the future it could
    be extended to select a specific register subset based on type.
    """
    for instr in function.body:
        if (
            isinstance(instr, Instruction)
            and isinstance(instr.function_target, AsmGlobalSymbol)
            and global_info.is_function_known_void(instr.function_target.symbol_name)
        ):
            instr.outputs.clear()


def setup_planned_vars(
    stack_info: StackInfo, persistent_state: PersistentFunctionState
) -> None:
    """Set up stack_info planned vars from state from earlier translation passes."""
    planned_vars = defaultdict(list)
    for key, persistent_var in persistent_state.planned_vars.items():
        planned_vars[persistent_var.get_representative()].append(key)

    for keys in planned_vars.values():
        reg = keys[0][0]
        reg_name = stack_info.function.reg_formatter.format(reg)
        prefix = f"var_{reg_name}"
        if stack_info.global_info.deterministic_vars:
            lineno = min(
                (
                    0
                    if not isinstance(source, InstrRef)
                    else source.instruction.meta.lineno
                )
                for _, source in keys
            )
            prefix = f"{prefix}_{lineno}"
        var = Var(
            stack_info,
            prefix=prefix,
            is_planned=True,
            type=Type.any_reg(),
        )
        stack_info.temp_vars.append(var)
        for key in keys:
            stack_info.planned_vars[key] = var


def setup_reg_vars(stack_info: StackInfo, options: Options) -> None:
    """Set up per-register planned vars based on command line flags."""
    arch = stack_info.global_info.arch

    if options.reg_vars == ["saved"]:
        reg_vars = arch.saved_regs
    elif options.reg_vars == ["most"]:
        reg_vars = arch.saved_regs + arch.simple_temp_regs
    elif options.reg_vars == ["all"]:
        reg_vars = arch.saved_regs + arch.simple_temp_regs + arch.argument_regs
    else:
        reg_vars = [
            stack_info.function.reg_formatter.parse(x, arch) for x in options.reg_vars
        ]
    for reg in reg_vars:
        reg_name = stack_info.function.reg_formatter.format(reg)
        stack_info.add_register_var(reg, reg_name)


def setup_initial_registers(state: NodeState, fn_sig: FunctionSignature) -> None:
    """Set up initial register contents for translation."""
    stack_info = state.stack_info
    arch = stack_info.global_info.arch

    state.set_initial_reg(
        arch.stack_pointer_reg,
        GlobalSymbol("sp", type=Type.ptr()),
        RegMeta(initial=True),
    )
    for reg in arch.saved_regs:
        state.set_initial_reg(
            reg, stack_info.saved_reg_symbol(reg.register_name), RegMeta(initial=True)
        )

    def make_arg(loc: ArgLoc, type: Type) -> PassedInArg:
        if loc.offset is not None:
            assert loc.offset % 4 == 0
        return PassedInArg(loc, stack_info=stack_info, type=type)

    abi = arch.function_abi(
        fn_sig,
        likely_regs={reg: True for reg in arch.argument_regs},
        for_call=False,
    )
    for slot in abi.arg_slots:
        type = stack_info.add_known_param(slot.loc, slot.name, slot.type)
        if slot.loc.reg is not None:
            state.set_initial_reg(
                slot.loc.reg,
                make_arg(slot.loc, type),
                RegMeta(uninteresting=True, initial=True),
            )
    for slot in abi.possible_slots:
        if slot.loc.reg is not None:
            state.set_initial_reg(
                slot.loc.reg,
                make_arg(slot.loc, slot.type),
                RegMeta(uninteresting=True, initial=True),
            )


def translate_to_ast(
    function: Function,
    flow_graph: FlowGraph,
    options: Options,
    global_info: GlobalInfo,
) -> FunctionInfo:
    """
    Given a function, produce a FlowGraph that both contains control-flow
    information and has AST transformations for each block of code and
    branch condition.
    """
    persistent_state = global_info.get_persistent_function_state(function.name)
    stack_info = get_stack_info(function, persistent_state, global_info, flow_graph)
    state = NodeState(
        node=flow_graph.entry_node(),
        regs=RegInfo(stack_info=stack_info),
        stack_info=stack_info,
    )

    setup_planned_vars(stack_info, persistent_state)
    setup_reg_vars(stack_info, options)

    fn_sym = global_info.address_of_gsym(function.name).expr
    assert isinstance(fn_sym, GlobalSymbol)

    fn_type = Type.function()
    fn_type.unify(fn_sym.type)
    fn_sig = Type.ptr(fn_type).get_function_pointer_signature()
    assert fn_sig is not None, "fn_type is known to be a function"
    stack_info.is_variadic = fn_sig.is_variadic

    setup_initial_registers(state, fn_sig)

    if options.debug:
        print(stack_info)
        print("\nNow, we attempt to translate:")

    used_naive_phis: List[NaivePhiExpr] = []
    return_blocks: List[BlockInfo] = []
    translate_all_blocks(state, used_naive_phis, return_blocks, options)

    arch = global_info.arch

    # Guess return register and mark returns (we should really base this on
    # function signature if known, but so far the heuristic is reliable enough)
    for reg, _ in arch.base_return_regs:
        propagate_register_meta(flow_graph.nodes, reg)

    return_type = fn_sig.return_type
    return_reg: Optional[Tuple[Register, bool]] = None

    if not options.void and not return_type.is_void():
        return_reg = determine_return_register(
            return_blocks, fn_sym.type_provided, arch
        )

    if return_reg is not None:
        reg, is_floating = return_reg
        if is_floating:
            return_type.unify(Type.floatish())
        else:
            return_type.unify(Type.intptr())

        for b in return_blocks:
            if reg in b.final_register_states:
                ret_val = b.final_register_states[reg]
                ret_val = as_var_type(ret_val, return_type, global_info.register_cuts)
                ret_val.use()
                b.return_value = ret_val
    else:
        return_type.unify(Type.void())

    assign_naive_phis(used_naive_phis, stack_info)

    # Guess parameters
    if not fn_sig.params_known:
        while len(fn_sig.params) < len(stack_info.arguments):
            fn_sig.params.append(FunctionParam())
        for param, arg in zip(fn_sig.params, stack_info.arguments):
            param.type.unify(arg.type)
            if not param.name:
                param.name = arg.format(Formatter())

    resolve_types_late(stack_info)

    if options.pdb_translate:
        import pdb

        v: Dict[str, object] = {}
        fmt = Formatter()
        for local in stack_info.local_vars:
            var_name = local.format(fmt)
            v[var_name] = local
        for var in stack_info.temp_vars:
            if var.is_emitted:
                var_name = var.format(fmt)
                exprs = var.debug_exprs
                v[var_name] = exprs[0] if len(exprs) == 1 else exprs
        for phi in stack_info.naive_phi_vars:
            assert phi.name is not None
            v[phi.name] = phi
        pdb.set_trace()

    return FunctionInfo(stack_info, flow_graph, return_type, fn_sym)
