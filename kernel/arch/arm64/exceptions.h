#ifndef GRACEOS_ARM64_EXCEPTIONS_H
#define GRACEOS_ARM64_EXCEPTIONS_H

/* Exception vector table base — installed into VBAR_EL1 by boot.S.
   Defined in exceptions.S, 2 KB aligned per AArch64 spec. */
extern void exception_vectors(void);

#endif /* GRACEOS_ARM64_EXCEPTIONS_H */
