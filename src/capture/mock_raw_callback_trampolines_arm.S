/* Shared ARM entry/return paths.  Per-slot stubs are generated separately. */
.syntax unified
.arm
.text
.align 2

.global mock_raw_callback_enter
.hidden mock_raw_callback_enter
.type mock_raw_callback_enter, %function
mock_raw_callback_enter:
    /* Stub already saved incoming r12/LR (8 bytes); preserve r0-r3. */
    stmdb sp!, {r0-r3}
    mrs r2, cpsr
    sub sp, sp, #8
    str r2, [sp, #0]
    str r2, [sp, #4]       /* deterministic pad, keeps public call aligned */
    mov r0, sp
    mov r1, r12
    bl mock_raw_callback_prepare

    /* prepare replaced saved r12/LR with target PC/return continuation. */
    ldr r0, [sp, #0]
    msr cpsr_f, r0
    add sp, sp, #8
    ldmia sp!, {r0-r3}
    ldmia sp!, {r12, lr}
    bx r12
.size mock_raw_callback_enter, .-mock_raw_callback_enter

.global mock_raw_callback_return
.hidden mock_raw_callback_return
.type mock_raw_callback_return, %function
mock_raw_callback_return:
    /* Preserve both ABI return lanes and caller-scratch state, aligned by 8. */
    stmdb sp!, {r0-r3, r12, lr}
    add r0, sp, #24
    mov r1, sp
    bl mock_raw_callback_complete
    mov lr, r0
    ldmia sp!, {r0-r3, r12}
    add sp, sp, #4
    bx lr
.size mock_raw_callback_return, .-mock_raw_callback_return
