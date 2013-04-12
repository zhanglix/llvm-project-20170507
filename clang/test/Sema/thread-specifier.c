// RUN: %clang_cc1 -triple i686-pc-linux-gnu -fsyntax-only -Wno-private-extern -verify -pedantic %s -DGNU
// RUN: %clang_cc1 -triple i686-pc-linux-gnu -fsyntax-only -Wno-private-extern -verify -pedantic -x c++ %s -DGNU
// RUN: %clang_cc1 -triple i686-pc-linux-gnu -fsyntax-only -Wno-private-extern -verify -pedantic %s -DC11 -D__thread=_Thread_local
// RUN: %clang_cc1 -triple i686-pc-linux-gnu -fsyntax-only -Wno-private-extern -verify -pedantic -x c++ %s -DC11 -D__thread=_Thread_local
// RUN: %clang_cc1 -triple i686-pc-linux-gnu -fsyntax-only -Wno-private-extern -verify -pedantic -x c++ %s -DCXX11 -D__thread=thread_local -std=c++11

#ifdef __cplusplus
// In C++, we define __private_extern__ to extern.
#undef __private_extern__
#endif

__thread int t1;
__thread extern int t2;
__thread static int t3;
#ifdef GNU
// expected-warning@-3 {{'__thread' before 'extern'}}
// expected-warning@-3 {{'__thread' before 'static'}}
#endif

__thread __private_extern__ int t4;
struct t5 { __thread int x; };
#ifdef __cplusplus
// expected-error-re@-2 {{'(__thread|_Thread_local|thread_local)' is only allowed on variable declarations}}
#else
// FIXME: The 'is only allowed on variable declarations' diagnostic is better here.
// expected-error@-5 {{type name does not allow storage class to be specified}}
#endif

__thread int t6();
#if defined(GNU)
// expected-error@-2 {{'__thread' is only allowed on variable declarations}}
#elif defined(C11)
// expected-error@-4 {{'_Thread_local' is only allowed on variable declarations}}
#else
// expected-error@-6 {{'thread_local' is only allowed on variable declarations}}
#endif

int f(__thread int t7) { // expected-error {{' is only allowed on variable declarations}}
  __thread int t8;
#if defined(GNU)
  // expected-error@-2 {{'__thread' variables must have global storage}}
#elif defined(C11)
  // expected-error@-4 {{'_Thread_local' variables must have global storage}}
#else
  // expected-error@-6 {{'thread_local' variables must have global storage}}
#endif
  extern __thread int t9;
  static __thread int t10;
  __thread __private_extern__ int t11;
#if __cplusplus < 201103L
  __thread auto int t12a; // expected-error-re {{cannot combine with previous '(__thread|_Thread_local)' declaration specifier}}
  auto __thread int t12b; // expected-error {{cannot combine with previous 'auto' declaration specifier}}
#else
  __thread auto t12a = 0; // expected-error {{'thread_local' variables must have global storage}}
  auto __thread t12b = 0; // expected-error {{'thread_local' variables must have global storage}}
#endif
  __thread register int t13a; // expected-error-re {{cannot combine with previous '(__thread|_Thread_local|thread_local)' declaration specifier}}
  register __thread int t13b; // expected-error {{cannot combine with previous 'register' declaration specifier}}
}

__thread typedef int t14; // expected-error-re {{cannot combine with previous '(__thread|_Thread_local|thread_local)' declaration specifier}}
__thread int t15; // expected-note {{previous definition is here}}
extern int t15; // expected-error {{non-thread-local declaration of 't15' follows thread-local declaration}}
extern int t16; // expected-note {{previous definition is here}}
__thread int t16; // expected-error {{thread-local declaration of 't16' follows non-thread-local declaration}}

// PR13720
__thread int thread_int;
int *thread_int_ptr = &thread_int;
#ifndef __cplusplus
// expected-error@-2 {{initializer element is not a compile-time constant}}
#endif
void g() {
  int *p = &thread_int; // This is perfectly fine, though.
}
#if __cplusplus >= 201103L
constexpr int *thread_int_ptr_2 = &thread_int; // expected-error {{must be initialized by a constant expression}}
#endif
