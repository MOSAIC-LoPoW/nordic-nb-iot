
/* auto-generated by gen_syscalls.py, don't edit */
#ifndef Z_INCLUDE_SYSCALLS_ATOMIC_H
#define Z_INCLUDE_SYSCALLS_ATOMIC_H


#ifndef _ASMLANGUAGE

#include <syscall_list.h>
#include <syscall_macros.h>

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic push
#endif

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern int z_impl_atomic_cas(atomic_t * target, atomic_val_t old_value, atomic_val_t new_value);
static inline int atomic_cas(atomic_t * target, atomic_val_t old_value, atomic_val_t new_value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (int) arch_syscall_invoke3(*(uintptr_t *)&target, *(uintptr_t *)&old_value, *(uintptr_t *)&new_value, K_SYSCALL_ATOMIC_CAS);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_cas(target, old_value, new_value);
}


extern atomic_val_t z_impl_atomic_add(atomic_t * target, atomic_val_t value);
static inline atomic_val_t atomic_add(atomic_t * target, atomic_val_t value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (atomic_val_t) arch_syscall_invoke2(*(uintptr_t *)&target, *(uintptr_t *)&value, K_SYSCALL_ATOMIC_ADD);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_add(target, value);
}


extern atomic_val_t z_impl_atomic_sub(atomic_t * target, atomic_val_t value);
static inline atomic_val_t atomic_sub(atomic_t * target, atomic_val_t value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (atomic_val_t) arch_syscall_invoke2(*(uintptr_t *)&target, *(uintptr_t *)&value, K_SYSCALL_ATOMIC_SUB);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_sub(target, value);
}


extern atomic_val_t z_impl_atomic_set(atomic_t * target, atomic_val_t value);
static inline atomic_val_t atomic_set(atomic_t * target, atomic_val_t value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (atomic_val_t) arch_syscall_invoke2(*(uintptr_t *)&target, *(uintptr_t *)&value, K_SYSCALL_ATOMIC_SET);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_set(target, value);
}


extern atomic_val_t z_impl_atomic_or(atomic_t * target, atomic_val_t value);
static inline atomic_val_t atomic_or(atomic_t * target, atomic_val_t value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (atomic_val_t) arch_syscall_invoke2(*(uintptr_t *)&target, *(uintptr_t *)&value, K_SYSCALL_ATOMIC_OR);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_or(target, value);
}


extern atomic_val_t z_impl_atomic_xor(atomic_t * target, atomic_val_t value);
static inline atomic_val_t atomic_xor(atomic_t * target, atomic_val_t value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (atomic_val_t) arch_syscall_invoke2(*(uintptr_t *)&target, *(uintptr_t *)&value, K_SYSCALL_ATOMIC_XOR);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_xor(target, value);
}


extern atomic_val_t z_impl_atomic_and(atomic_t * target, atomic_val_t value);
static inline atomic_val_t atomic_and(atomic_t * target, atomic_val_t value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (atomic_val_t) arch_syscall_invoke2(*(uintptr_t *)&target, *(uintptr_t *)&value, K_SYSCALL_ATOMIC_AND);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_and(target, value);
}


extern atomic_val_t z_impl_atomic_nand(atomic_t * target, atomic_val_t value);
static inline atomic_val_t atomic_nand(atomic_t * target, atomic_val_t value)
{
#ifdef CONFIG_USERSPACE
	if (z_syscall_trap()) {
		return (atomic_val_t) arch_syscall_invoke2(*(uintptr_t *)&target, *(uintptr_t *)&value, K_SYSCALL_ATOMIC_NAND);
	}
#endif
	compiler_barrier();
	return z_impl_atomic_nand(target, value);
}


#ifdef __cplusplus
}
#endif

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic pop
#endif

#endif
#endif /* include guard */
