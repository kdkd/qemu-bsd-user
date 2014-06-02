#ifndef _TARGET_OS_STACK_H_
#define _TARGET_OS_STACK_H_

#include <sys/param.h>
#include "target_arch_sigtramp.h"

/*
 * The inital FreeBSD stack is as follows:
 * (see kern/kern_exec.c exec_copyout_strings() )
 *
 *  Hi Address -> char **ps_argvstr  (struct ps_strings for ps, w, etc.)
 *                unsigned ps_nargvstr
 *                char **ps_envstr
 *  PS_STRINGS -> unsigned ps_nenvstr
 *
 *                machine dependent sigcode (sv_sigcode of size
 *                                           sv_szsigcode)
 *
 *                execpath          (absolute image path for rtld)
 *
 *                SSP Canary        (sizeof(long) * 8)
 *
 *                page sizes array  (usually sizeof(u_long) )
 *
 *  "destp" ->    argv, env strings (up to 262144 bytes)
 */
static inline int setup_initial_stack(struct bsd_binprm *bprm,
        abi_ulong *ret_addr)
{
    int i;
    abi_ulong stack_hi_addr;
    size_t execpath_len, stringspace;
    abi_ulong destp, argvp, envp, p;
    struct target_ps_strings ps_strs;
    char canary[sizeof(abi_long) * 8];

    stack_hi_addr = p = target_stkbas + target_stksiz;

    /* Save some space for ps_strings. */
    p -= sizeof(struct target_ps_strings);

#ifdef TARGET_SZSIGCODE
    /* Add machine depedent sigcode. */
    p -= TARGET_SZSIGCODE;
    if (setup_sigtramp(p, (unsigned)offsetof(struct target_sigframe, sf_uc),
            TARGET_FREEBSD_NR_sigreturn)) {
        errno = EFAULT;
        return -1;
    }
#endif
    if (bprm->fullpath) {
        execpath_len = strlen(bprm->fullpath) + 1;
        p -= roundup(execpath_len, sizeof(abi_ulong));
        if (memcpy_to_target(p, bprm->fullpath, execpath_len)) {
            errno = EFAULT;
            return -1;
        }
    }
    /* Add canary for SSP. */
    arc4random_buf(canary, sizeof(canary));
    p -= roundup(sizeof(canary), sizeof(abi_ulong));
    if (memcpy_to_target(p, canary, sizeof(canary))) {
        errno = EFAULT;
        return -1;
    }
    /* Add page sizes array. */
    /* p -= sizeof(int); */
    p -= sizeof(abi_ulong);
    /* if (put_user_u32(TARGET_PAGE_SIZE, p)) { */
    if (put_user_ual(TARGET_PAGE_SIZE, p)) {
        errno = EFAULT;
        return -1;
    }
    /* Calculate the string space needed */
    stringspace = 0;
    for (i = 0; i < bprm->argc; ++i) {
        stringspace += strlen(bprm->argv[i]) + 1;
    }
    for (i = 0; i < bprm->envc; ++i) {
        stringspace += strlen(bprm->envp[i]) + 1;
    }
    if (stringspace > TARGET_ARG_MAX) {
       errno = ENOMEM;
       return -1;
    }

    /* Make room for the argv and envp strings */
    /* p = destp = roundup(p - TARGET_SPACE_USRSPACE - (TARGET_ARG_MAX - stringspace), sizeof(abi_ulong)); */
    argvp = p - TARGET_SPACE_USRSPACE;
    p = destp = roundup(p - TARGET_SPACE_USRSPACE - TARGET_ARG_MAX, sizeof(abi_ulong));

    /*
     * Add argv strings.  Note that the argv[] vectors are added by
     * loader_build_argptr()
     */
    /* XXX need to make room for auxargs */
    /* argvp = destp - ((bprm->argc + bprm->envc + 2) * sizeof(abi_ulong)); */
    /* envp = argvp + (bprm->argc + 2) * sizeof(abi_ulong); */
    envp = argvp + (bprm->argc + 1) * sizeof(abi_ulong);
    ps_strs.ps_argvstr = tswapl(argvp);
    ps_strs.ps_nargvstr = tswap32(bprm->argc);
    for (i = 0; i < bprm->argc; ++i) {
        size_t len = strlen(bprm->argv[i]) + 1;

        if (memcpy_to_target(destp, bprm->argv[i], len)) {
            errno = EFAULT;
            return -1;
        }
        if (put_user_ual(destp, argvp)) {
            errno = EFAULT;
            return -1;
        }
        argvp += sizeof(abi_ulong);
        destp += len;
    }
    if (put_user_ual(0, argvp)) {
        errno = EFAULT;
        return -1;
    }
    /*
     * Add env strings. Note that the envp[] vectors are added by
     * loader_build_argptr().
     */
    ps_strs.ps_envstr = tswapl(envp);
    ps_strs.ps_nenvstr = tswap32(bprm->envc);
    for (i = 0; i < bprm->envc; ++i) {
        size_t len = strlen(bprm->envp[i]) + 1;

        if (memcpy_to_target(destp, bprm->envp[i], len)) {
            errno = EFAULT;
            return -1;
        }
        if (put_user_ual(destp, envp)) {
            errno = EFAULT;
            return -1;
        }
        envp += sizeof(abi_ulong);
        destp += len;
    }
    if (put_user_ual(0, envp)) {
        errno = EFAULT;
        return -1;
    }
    if (memcpy_to_target(stack_hi_addr - sizeof(ps_strs), &ps_strs,
                sizeof(ps_strs))) {
        errno = EFAULT;
        return -1;
    }

    if (ret_addr) {
       *ret_addr = p;
    }

    return 0;
 }

#endif /* !_TARGET_OS_STACK_H_ */