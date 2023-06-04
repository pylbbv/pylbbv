import dis

class TestInfo:
    def __init__(self, msg: str):
        self.msg = msg
    def __enter__(self):
        print(f"Testing {self.msg}...", end="")
        return self
    def __exit__(self, exc_type, exc_value, traceback):
        if exc_value is not None:
            print(":( failed!")
        else:
            print("done!")


print("Begin tests...")

#########
# Utils #
#########

def trigger_tier2(f, args):
    for _ in range(64):
        f(*args)

def writeinst(opc:str, arg:int=0):

    "Makes life easier in writing python bytecode"

    nb = max(1,-(-arg.bit_length()//8))
    ab = arg.to_bytes(nb, 'big')
    ext_arg = dis._all_opmap['EXTENDED_ARG']
    inst = bytearray()
    for i in range(nb-1):
        inst.append(ext_arg)
        inst.append(ab[i])
    inst.append(dis._all_opmap[opc])
    inst.append(ab[-1])

    return bytes(inst)


################################################
# Type prop tests: TYPE_SET and TYPE_OVERWRITE #
################################################
with TestInfo("type propagation"):

    def test_typeprop1(a):
        # Dummy code won't be ran
        return a+(a+(a+a))

    bytecode = b"".join([
        # Tests TYPE_SET and TYPE_OVERWRITE
        writeinst("RESUME", 0),
        writeinst("LOAD_FAST", 0),
        writeinst("COPY", 1),
        writeinst("COPY", 1),
        writeinst("BINARY_OP", 0),
        writeinst("CACHE", 0), # For tier1
        writeinst("BINARY_OP", 0),
        writeinst("CACHE", 0), # For tier1
        writeinst("RETURN_VALUE", 0)
    ])

    # Switch to bytecode
    test_typeprop1.__code__ = test_typeprop1.__code__.replace(co_code=bytecode)

    trigger_tier2(test_typeprop1, (0,))
    expected = [
        "RESUME_QUICK",
        "LOAD_FAST", # Load locals
        "COPY",
        "COPY", # Copy variable on stack
                # All stack variables part of the tree
        "CHECK_FLOAT",
        "NOP",  # Space for an EXTENDED_ARG if needed
        "BB_BRANCH_IF_FLAG_SET",

        # This should let the typeprop know all the locals and stack be int
        # TYPE_SET
        # Locals: [int]
        # Stack : [int->locals[0], int->stack[0], int->stack[1]]
        "CHECK_INT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET", # Fallthrough!

        # Should propagate the result as int
        # TYPE_OVERWRITE
        # Locals: [int]
        # Stack : [int->locals[0], int]
        "BINARY_OP_ADD_INT_REST",

        # There should be no more guards here
        # if the type propagator is working
        "BINARY_OP_ADD_INT_REST",
        "RETURN_VALUE"
    ]
    insts = dis.get_instructions(test_typeprop1, tier2=True)
    for x,y in zip(insts, expected):
        assert x.opname == y

    ################################################
    # Type prop tests: TYPE_SWAP                   #
    ################################################

    bytecode = b"".join([
        # Tests TYPE_SWAP
        writeinst("RESUME", 0),
        writeinst("LOAD_FAST", 0), # float
        writeinst("LOAD_FAST", 1), # int
        writeinst("SWAP", 2), # Stack: [int, float]

        writeinst("COPY", 1),
        # Should generate the FLOAT specialisation
        writeinst("BINARY_OP", 0),
        writeinst("CACHE", 0), # For tier1

        writeinst("SWAP", 2), # [float, int]
        writeinst("COPY", 1),
        # Should generate the INT specialisation
        writeinst("BINARY_OP", 0),
        writeinst("CACHE", 0), # For tier1

        # float + int
        writeinst("BINARY_OP", 0),
        writeinst("CACHE", 0), # For tier1
        writeinst("RETURN_VALUE", 0)
    ])

    def test_typeprop2(a,b):
        # Dummy code won't be ran
        return a+(a+(a+a))

    # Switch to bytecode
    test_typeprop2.__code__ = test_typeprop2.__code__.replace(co_code=bytecode)
    test_typeprop2(0.1,1)

    trigger_tier2(test_typeprop2, (0.1,1))
    expected = [
        "RESUME_QUICK",
        "LOAD_FAST",
        "LOAD_FAST",
        "SWAP",
        "COPY",

        # Should gen specialised float
        "CHECK_FLOAT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET",
        "UNBOX_FLOAT",
        "BINARY_OP_ADD_FLOAT_UNBOXED",
        "SWAP",
        "COPY",

        # Ladder of types guards
        "CHECK_FLOAT",
        "NOP",
        "BB_BRANCH_IF_FLAG_SET",

        # Should gen specialised int
        "CHECK_INT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET",
        "BINARY_OP_ADD_INT_REST",
        # Don't care about the rest of the insts
    ]
    insts = dis.get_instructions(test_typeprop2, tier2=True)
    # Assert the value is correct
    assert abs(test_typeprop2(0.1,1) - 2.2) < 0.001
    for x,y in zip(insts, expected):
        assert x.opname == y


#######################################
# Tests for: Type guard               #
# + Float unboxing                    #
# + Jump rewriting test               #
# + Tier2 guard stability             #
#######################################
with TestInfo("type guard"):

    def test_guard_elimination(a,b):
        x = b
        y = b
        # First a+x should inform the type prop that
        # `a`, `x`, `b` and `y` are int
        # So guard should be eliminated in (a+x) + y
        return a + x + y

    trigger_tier2(test_guard_elimination, (0,0))
    expected = [
        # From tier1 bytecode
        "RESUME_QUICK",
        "LOAD_FAST",
        "STORE_FAST",
        "LOAD_FAST",
        "STORE_FAST",
        "LOAD_FAST",
        "LOAD_FAST",

        "CHECK_FLOAT", # First ladder check
        "NOP",
        "BB_BRANCH_IF_FLAG_SET",
        "CHECK_INT", # Second ladder check
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET", # Fall through!
        "CHECK_INT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET", # Fall through!
        "BINARY_OP_ADD_INT_REST", # a+x
        "LOAD_FAST",
        "BINARY_OP_ADD_INT_REST", # (a+x) + y (guard eliminated)
        "RETURN_VALUE"
    ]
    insts = dis.get_instructions(test_guard_elimination, tier2=True)
    for x,y in zip(insts, expected):
        assert x.opname == y

    # We only wanna test the stability of the first type guards
    # later on
    first_guard_test_until = insts[-1].offset

    # Trigger generation of other branch
    test_guard_elimination(0.1, 0.1)
    insts = dis.get_instructions(test_guard_elimination, tier2=True)
    expected = [
        # From tier1 bytecode
        "RESUME_QUICK",
        "LOAD_FAST",
        "STORE_FAST",
        "LOAD_FAST",
        "STORE_FAST",
        "LOAD_FAST",
        "LOAD_FAST",

        "CHECK_FLOAT", # First ladder check
        "NOP",
        "BB_JUMP_IF_FLAG_SET", # Rewrite to jump to float case
        "POP_TOP", # Pop result

        # The same as above
        "CHECK_INT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET",
        "CHECK_INT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET",    
        "BINARY_OP_ADD_INT_REST",
        "LOAD_FAST",
        "BINARY_OP_ADD_INT_REST",
        "RETURN_VALUE",

        # Float case
        "CHECK_FLOAT", # First ladder check
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET", # Rewrite to jump to float case
        "BINARY_OP_ADD_FLOAT_UNBOXED", # Unbox
        "LOAD_FAST",
        "UNBOX_FLOAT", # Unbox local
        "STORE_FAST_UNBOXED_BOXED", # Store unboxed float into local
        "LOAD_FAST_NO_INCREF", # Load (unboxed) local again
        "BINARY_OP_ADD_FLOAT_UNBOXED", # No type guard here
        "BOX_FLOAT", # Box to return
        "RETURN_VALUE"
    ]

    test_guard_elimination(1,1)
    for x,y in zip(insts, expected):
        assert x.opname == y

    # Perform other polymorphism stuff
    # We've not implemented type guard elimination
    # For these mixed types (e.g., float+int) 
    # So these will generate more type guards with the same 
    # mechanisms as above.
    # So codegen wise tier2 takes a while to stabilise
    assert (test_guard_elimination(1,0.1) - 1.2) < 0.001
    assert (test_guard_elimination(0.1,1) - 2.1) < 0.001
    assert (test_guard_elimination(.4,.5) - 1.4) < 0.001
    assert test_guard_elimination(2,3) == 8

    # At this point all cases should be generated
    # so check if the generated cases are the same
    expected = dis.get_instructions(test_guard_elimination, tier2=True)
    test_guard_elimination(-192,203)
    test_guard_elimination(2.3, 12)
    test_guard_elimination(324, 0.12)
    test_guard_elimination(0.12,32.1)
    insts = dis.get_instructions(test_guard_elimination, tier2=True)

    # Make sure the first type guard is stable
    for x,y in zip(insts, expected):
        if x.offset >= first_guard_test_until:
            break
        assert x.opname == y.opname


##############################
# Test: Backward jump offset #
##############################
with TestInfo("backward jump"):

    def test_backwards_jump(a):
        for i in range(64):
            a = i + a
        return a

    # Trigger only one JUMP_BACKWARD_QUICK
    # i.e., perfect specialisation the first time
    trigger_tier2(test_backwards_jump, (0,))

    # Make sure it looped 64 times
    assert test_backwards_jump(7) == 2023 # <-- Hi! ~ Jules

    # Make sure it jumped to the correct spot
    insts = dis.get_instructions(test_backwards_jump, tier2=True) 
    backwards_jump = next(x for x in insts if x.opname == "JUMP_BACKWARD_QUICK")
    instidx, jmp_target = next((i,x) for i,x in enumerate(insts) if x.offset == backwards_jump.argval)
    assert jmp_target.opname == "NOP" # Space for an EXTENDED_ARG
    assert insts[instidx + 1].opname == "BB_TEST_ITER_RANGE" # The loop predicate

######################
# Test: Loop peeling #
######################
with TestInfo("loop peeling"):
    def test_loop_peeling(a):
        for i in range(64):
            a = float(i) + a
        return a

    # This triggers loop peeling, because 
    # the first iteration `a` type is int
    # and the 2nd iteration `a` type is float
    # This should triger a JUMP_FORWARD in place of
    # a JUMP_BACKWARD_QUICK
    trigger_tier2(test_loop_peeling, (0,))

    # Make sure it looped 64 times
    assert abs(test_loop_peeling(7) - 2023) < 0.001

    # Make sure the JUMP_FORWARD jumped correctly
    insts = dis.get_instructions(test_loop_peeling, tier2=True) 
    forwards_jump = next(x for x in insts if x.opname == "JUMP_FORWARD")
    instidx, jmp_target = next((i,x) for i,x in enumerate(insts) if x.offset == forwards_jump.argval)
    assert jmp_target.opname == "NOP" # Space for an EXTENDED_ARG
    assert insts[instidx + 1].opname == "BB_TEST_ITER_RANGE" # The loop predicate

    # We also need to make sure JUMP_FORWARD
    # jumped into the float-specialised loop body
    endidx, _ = next(
        (i,x) for i,x in enumerate(insts) 
        if (x.opname == "JUMP_BACKWARD_QUICK" and x.offset > jmp_target.offset))
    # Check for existence of float-specialised instruction in loop body
    assert any(1 for _ in
        filter(lambda i: i.opname == 'BINARY_OP_ADD_FLOAT_UNBOXED', insts[instidx:endidx]))


##################################
# Test: Container specialisation #
##################################
with TestInfo("container specialisation"):
    def test_container(l):
        l[2] = l[0] + l[1]


    trigger_tier2(test_container, ([1,2,3,4],))
    insts = dis.get_instructions(test_container, tier2=True)
    expected = [
        "RESUME_QUICK",
        "LOAD_FAST",
        "LOAD_CONST",

        "CHECK_LIST",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET", # Fallthrough!

        # Type prop from const array: No type guard needed
        "BINARY_SUBSCR_LIST_INT_REST",
        "LOAD_FAST",
        "LOAD_CONST",
        # CHECK_LIST should eliminate the type guard here
        "BINARY_SUBSCR_LIST_INT_REST",

        # We haven't implemented type prop into container types
        # so these checks should get generated
        "CHECK_FLOAT",
        "NOP",
        "BB_BRANCH_IF_FLAG_SET",
        "CHECK_INT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET",
        "CHECK_INT",
        "NOP",
        "BB_BRANCH_IF_FLAG_UNSET",
        "BINARY_OP_ADD_INT_REST",
        
        "LOAD_FAST",
        "LOAD_CONST",
        # CHECK_LIST should eliminate the type guard here
        "STORE_SUBSCR_LIST_INT_REST",
        "RETURN_CONST",
    ]
    for x,y in zip(insts, expected):
        assert x.opname == y

####################################################
# Tests for: Tier 2 BB_TEST_ITER specialisation    #
####################################################
with TestInfo("BB_TEST_ITER specialisation"):
    lst = [1, 2, 3]
    def test_iter_list(a):
        for i in lst:
            a = i + a
        return a

    # Trigger only one JUMP_BACKWARD_QUICK
    # i.e., perfect specialisation the first time
    trigger_tier2(test_iter_list, (0,))

    # Make sure it looped 64 times
    assert test_iter_list(0) == 6

    # Make sure it jumped to the correct spot
    insts = dis.get_instructions(test_iter_list, tier2=True) 
    backwards_jump = next(x for x in insts if x.opname == "JUMP_BACKWARD_QUICK")
    instidx, jmp_target = next((i,x) for i,x in enumerate(insts) if x.offset == backwards_jump.argval)
    assert jmp_target.opname == "NOP" # Space for an EXTENDED_ARG
    assert insts[instidx + 1].opname == "BB_TEST_ITER_LIST" # The loop predicate


    def test_iter_tuple(a):
        for i in (1, 2, 3):
            a = i + a
        return a

    # Trigger only one JUMP_BACKWARD_QUICK
    # i.e., perfect specialisation the first time
    trigger_tier2(test_iter_tuple, (0,))

    # Make sure it looped 64 times
    assert test_iter_tuple(0) == 6

    # Make sure it jumped to the correct spot
    insts = dis.get_instructions(test_iter_tuple, tier2=True) 
    backwards_jump = next(x for x in insts if x.opname == "JUMP_BACKWARD_QUICK")
    instidx, jmp_target = next((i,x) for i,x in enumerate(insts) if x.offset == backwards_jump.argval)
    assert jmp_target.opname == "NOP" # Space for an EXTENDED_ARG
    assert insts[instidx + 1].opname == "BB_TEST_ITER_TUPLE" # The loop predicate

######################################################################
# Tests for: Tier 2 backward jump type context compatiblity check    #
######################################################################
with TestInfo("type context backwards jump compatibility check"):
    # See https://github.com/pylbbv/pylbbv/issues/9 for more information.
    def f(y,z,w):
        d = z
        for _ in [1,2]:
            z+z
            d+d
            d=w


    trigger_tier2(f, (1,1,1.))

    # As long as it doesn't crash, everything's good.

######################################################################
# Tests for: Tier 2 unconditional forward jump                       #
######################################################################
with TestInfo("tier 2 unconditional forward jumps"):
    # See https://github.com/pylbbv/pylbbv/issues/17 for more information.
    def f(x):
        for _ in [1]:
            break
        x+x # Force it to be optimisable

    trigger_tier2(f, (1,))

    # As long as it doesn't crash, everything's good.
print("Tests completed ^-^")