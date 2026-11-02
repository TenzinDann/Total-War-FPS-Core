    .intel_syntax noprefix
    .text
    .macro FWD name
    .globl proxy_\name
    .def proxy_\name; .scl 2; .type 32; .endef
proxy_\name:
    jmp qword ptr [rip + p_\name]
    .endm
    FWD GetFileVersionInfoA
    FWD GetFileVersionInfoByHandle
    FWD GetFileVersionInfoExA
    FWD GetFileVersionInfoExW
    FWD GetFileVersionInfoSizeA
    FWD GetFileVersionInfoSizeExA
    FWD GetFileVersionInfoSizeExW
    FWD GetFileVersionInfoSizeW
    FWD GetFileVersionInfoW
    FWD VerFindFileA
    FWD VerFindFileW
    FWD VerInstallFileA
    FWD VerInstallFileW
    FWD VerLanguageNameA
    FWD VerLanguageNameW
    FWD VerQueryValueA
    FWD VerQueryValueW
    .globl hfps_sim_tick_thunk
    .def hfps_sim_tick_thunk; .scl 2; .type 32; .endef
hfps_sim_tick_thunk:
    mov rax, qword ptr [rsp]
    cmp rax, qword ptr [rip + g_sim_tick_ret]
    je hfps_sim_tick_slow
    jmp qword ptr [rip + g_orig_sim_tick]
    .globl hfps_sim_tick_slow
    .def hfps_sim_tick_slow; .scl 2; .type 32; .endef
    .seh_proc hfps_sim_tick_slow
hfps_sim_tick_slow:
    push rcx
    .seh_pushreg rcx
    push rdx
    .seh_pushreg rdx
    push r8
    .seh_pushreg r8
    push r9
    .seh_pushreg r9
    sub rsp, 104
    .seh_stackalloc 104
    .seh_endprologue
    movdqu xmmword ptr [rsp + 32], xmm0
    movdqu xmmword ptr [rsp + 48], xmm1
    movdqu xmmword ptr [rsp + 64], xmm2
    movdqu xmmword ptr [rsp + 80], xmm3
    mov rdx, rsi
    mov r8, rdi
    call hfps_sim_tick
    movdqu xmm0, xmmword ptr [rsp + 32]
    movdqu xmm1, xmmword ptr [rsp + 48]
    movdqu xmm2, xmmword ptr [rsp + 64]
    movdqu xmm3, xmmword ptr [rsp + 80]
    add rsp, 104
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp qword ptr [rip + g_orig_sim_tick]
    .seh_endproc
    .globl hfps_post_step_thunk
    .def hfps_post_step_thunk; .scl 2; .type 32; .endef
hfps_post_step_thunk:
    mov rax, qword ptr [rsp]
    cmp rax, qword ptr [rip + g_post_step_ret]
    je hfps_post_step_slow
    jmp qword ptr [rip + g_orig_post_step]
    .globl hfps_post_step_slow
    .def hfps_post_step_slow; .scl 2; .type 32; .endef
    .seh_proc hfps_post_step_slow
hfps_post_step_slow:
    push rcx
    .seh_pushreg rcx
    push rdx
    .seh_pushreg rdx
    push r8
    .seh_pushreg r8
    push r9
    .seh_pushreg r9
    sub rsp, 104
    .seh_stackalloc 104
    .seh_endprologue
    movdqu xmmword ptr [rsp + 32], xmm0
    movdqu xmmword ptr [rsp + 48], xmm1
    movdqu xmmword ptr [rsp + 64], xmm2
    movdqu xmmword ptr [rsp + 80], xmm3
    mov rdx, rbx
    call hfps_post_step_begin
    movdqu xmm0, xmmword ptr [rsp + 32]
    movdqu xmm1, xmmword ptr [rsp + 48]
    movdqu xmm2, xmmword ptr [rsp + 64]
    movdqu xmm3, xmmword ptr [rsp + 80]
    add rsp, 104
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp qword ptr [rip + g_orig_post_step]
    .seh_endproc
    .globl hfps_hero_1c8_thunk
    .def hfps_hero_1c8_thunk; .scl 2; .type 32; .endef
hfps_hero_1c8_thunk:
    mov rax, qword ptr [rsp]
    cmp rax, qword ptr [rip + g_hero_1c8_ret1]
    je hfps_hero_1c8_slow
    cmp rax, qword ptr [rip + g_hero_1c8_ret2]
    je hfps_hero_1c8_slow
    jmp qword ptr [rip + g_orig_hero_1c8]
    .globl hfps_hero_1c8_slow
    .def hfps_hero_1c8_slow; .scl 2; .type 32; .endef
    .seh_proc hfps_hero_1c8_slow
hfps_hero_1c8_slow:
    push rcx
    .seh_pushreg rcx
    push rdx
    .seh_pushreg rdx
    push r8
    .seh_pushreg r8
    push r9
    .seh_pushreg r9
    sub rsp, 104
    .seh_stackalloc 104
    .seh_endprologue
    movdqu xmmword ptr [rsp + 32], xmm0
    movdqu xmmword ptr [rsp + 48], xmm1
    movdqu xmmword ptr [rsp + 64], xmm2
    movdqu xmmword ptr [rsp + 80], xmm3
    mov rdx, rsi
    call hfps_hero_melee
    movdqu xmm0, xmmword ptr [rsp + 32]
    movdqu xmm1, xmmword ptr [rsp + 48]
    movdqu xmm2, xmmword ptr [rsp + 64]
    movdqu xmm3, xmmword ptr [rsp + 80]
    add rsp, 104
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp qword ptr [rip + g_orig_hero_1c8]
    .seh_endproc
    .globl hfps_after_step_thunk
    .def hfps_after_step_thunk; .scl 2; .type 32; .endef
hfps_after_step_thunk:
    mov rax, qword ptr [rsp]
    cmp rax, qword ptr [rip + g_after_step_ret]
    je hfps_after_step_slow
    jmp qword ptr [rip + g_orig_after_step]
    .globl hfps_after_step_slow
    .def hfps_after_step_slow; .scl 2; .type 32; .endef
    .seh_proc hfps_after_step_slow
hfps_after_step_slow:
    push rcx
    .seh_pushreg rcx
    push rdx
    .seh_pushreg rdx
    push r8
    .seh_pushreg r8
    push r9
    .seh_pushreg r9
    sub rsp, 104
    .seh_stackalloc 104
    .seh_endprologue
    movdqu xmmword ptr [rsp + 32], xmm0
    movdqu xmmword ptr [rsp + 48], xmm1
    movdqu xmmword ptr [rsp + 64], xmm2
    movdqu xmmword ptr [rsp + 80], xmm3
    mov rdx, rdi
    call hfps_after_step_pre
    movdqu xmm0, xmmword ptr [rsp + 32]
    movdqu xmm1, xmmword ptr [rsp + 48]
    movdqu xmm2, xmmword ptr [rsp + 64]
    movdqu xmm3, xmmword ptr [rsp + 80]
    add rsp, 104
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp qword ptr [rip + g_orig_after_step]
    .seh_endproc
    .globl hfps_cam_update_thunk
    .def hfps_cam_update_thunk; .scl 2; .type 32; .endef
    .seh_proc hfps_cam_update_thunk
hfps_cam_update_thunk:
    push rbx
    .seh_pushreg rbx
    push rsi
    .seh_pushreg rsi
    sub rsp, 40
    .seh_stackalloc 40
    .seh_endprologue
    mov rbx, rcx
    call qword ptr [rip + g_orig_cam_update]
    mov rsi, rax
    mov rcx, rbx
    call hfps_after_cam_update
    mov rax, rsi
    add rsp, 40
    pop rsi
    pop rbx
    ret
    .seh_endproc
