// RUN: %clang_cc1            -verify=expected,both %s -fexperimental-new-constant-interpreter
// RUN: %clang_cc1 -std=c++20 -verify=expected,both %s -fexperimental-new-constant-interpreter
// RUN: %clang_cc1            -verify=ref,both      %s
// RUN: %clang_cc1 -std=c++20 -verify=ref,both      %s

union U {
  int a;
  int b;
};

constexpr U a = {12};
static_assert(a.a == 12, "");
static_assert(a.b == 0, ""); // both-error {{not an integral constant expression}} \
                             // both-note {{read of member 'b' of union with active member 'a'}}
union U1 {
  int i;
  float f = 3.0f;
};
constexpr U1 u1{};
static_assert(u1.f == 3.0, "");
static_assert(u1.i == 1, ""); // both-error {{not an integral constant expression}} \
                              // both-note {{read of member 'i' of union with active member 'f'}}



union A {
  int a;
  double d;
};
constexpr A aa = {1, 2.0}; // both-error {{excess elements in union initializer}}
constexpr A ab = {.d = 1.0};
static_assert(ab.d == 1.0, "");
static_assert(ab.a == 1, ""); // both-error {{not an integral constant expression}} \
                              // both-note {{read of member 'a' of union with active member 'd'}}


namespace Empty {
  union E {};
  constexpr E e{};
}

namespace SimpleStore {
  union A {
    int a;
    int b;
  };
  constexpr int foo() {
    A a{.b = 4};
    a.b = 10;
    return a.b;
  }
  static_assert(foo() == 10, "");

  constexpr int empty() {
    A a{}; /// Just test that this works.
    return 10;
  }
  static_assert(empty() == 10, "");
}

namespace ZeroInit {
  struct S { int m; };
  union Z {
    float f;
  };

  constexpr Z z{};
  static_assert(z.f == 0.0, "");
}

namespace DefaultInit {
  union U1 {
    constexpr U1() {}
    int a, b = 42;
  };

  constexpr U1 u1; /// OK.

  constexpr int foo() { // expected-error {{never produces a constant expression}}
    U1 u;
    return u.a; // both-note {{read of member 'a' of union with active member 'b'}} \
                // expected-note {{read of member 'a' of union with active member 'b'}}
  }
  static_assert(foo() == 42); // both-error {{not an integral constant expression}} \
                              // both-note {{in call to}}
}

#if __cplusplus >= 202002L
namespace SimpleActivate {
  constexpr int foo() { // both-error {{never produces a constant expression}}
    union {
      int a;
      int b;
    } Z;

    Z.a = 10;
    Z.b = 20;
    return Z.a; // both-note 2{{read of member 'a' of union with active member 'b'}}
  }
  static_assert(foo() == 20); // both-error {{not an integral constant expression}} \
                              // both-note {{in call to}}

  constexpr int foo2() {
    union {
      int a;
      int b;
    } Z;

    Z.a = 10;
    Z.b = 20;
    return Z.b;
  }
  static_assert(foo2() == 20);


  constexpr int foo3() {
    union {
      struct {
        float x,y;
      } a;
      int b;
    } Z;

    Z.a.y = 10;

    return Z.a.x; // both-note {{read of uninitialized object}}
  }
  static_assert(foo3() == 10); // both-error {{not an integral constant expression}} \
                               // both-note {{in call to}}

  constexpr int foo4() {
    union {
      struct {
        float x,y;
      } a;
      int b;
    } Z;

    Z.a.x = 100;
    Z.a.y = 10;

    return Z.a.x;
  }
  static_assert(foo4() == 100);
}

namespace IndirectFieldDecl {
  struct C {
    union { int a, b = 2, c; };
    union { int d, e = 5, f; };
    constexpr C() : a(1) {}
  };
  static_assert(C().a == 1, "");
}

namespace UnionDtor {

  union U {
    int *I;
    constexpr U(int *I) : I(I) {}
    constexpr ~U() {
      *I = 10;
    }
  };

  constexpr int foo() {
    int a = 100;
    {
      U u(&a);
    }
    return a;
  }
  static_assert(foo() == 10);
}

namespace UnionMemberDtor {
  class UM {
  public:
    int &I;
    constexpr UM(int &I) : I(I) {}
    constexpr ~UM() { I = 200; }
  };

  union U {
    UM um;
    constexpr U(int &I) : um(I) {}
    constexpr ~U() {
    }
  };

  constexpr int foo() {
    int a = 100;
    {
      U u(a);
    }

    return a;
  }
  static_assert(foo() == 100);
}

namespace Nested {
  union U {
    int a;
    int b;
  };

  union U2 {
    U u;
    U u2;
    int x;
    int y;
  };

 constexpr int foo() { // both-error {{constexpr function never produces a constant expression}}
    U2 u;
    u.u.a = 10;
    int a = u.y; // both-note 2{{read of member 'y' of union with active member 'u' is not allowed in a constant expression}}

    return 1;
  }
  static_assert(foo() == 1); // both-error {{not an integral constant expression}} \
                             // both-note {{in call to}}

 constexpr int foo2() {
    U2 u;
    u.u.a = 10;
    return u.u.a;
  }
  static_assert(foo2() == 10);

 constexpr int foo3() { // both-error {{constexpr function never produces a constant expression}}
    U2 u;
    u.u.a = 10;
    int a = u.u.b; // both-note 2{{read of member 'b' of union with active member 'a' is not allowed in a constant expression}}

    return 1;
  }
  static_assert(foo3() == 1); // both-error {{not an integral constant expression}} \
                              // both-note {{in call to}}

  constexpr int foo4() { // both-error {{constexpr function never produces a constant expression}}
    U2 u;

    u.x = 10;

    return u.u.a; // both-note 2{{read of member 'u' of union with active member 'x' is not allowed in a constant expression}}
  }
  static_assert(foo4() == 1); // both-error {{not an integral constant expression}} \
                              // both-note {{in call to}}

}


namespace Zeroing {
  struct non_trivial_constructor {
      constexpr non_trivial_constructor() : x(100) {}
      int x;
  };
  union U2 {
      int a{1000};
      non_trivial_constructor b;
  };

  static_assert(U2().b.x == 100, ""); // both-error {{not an integral constant expression}} \
                                      // both-note {{read of member 'b' of union with active member 'a'}}

  union { int a; int b; } constexpr u1{};
  static_assert(u1.a == 0, "");
  static_assert(u1.b == 0, ""); // both-error {{not an integral constant expression}} \
                                // both-note {{read of member 'b' of union with active member 'a'}}

  union U { int a; int b; } constexpr u2 = U();
  static_assert(u2.a == 0, "");
  static_assert(u2.b == 0, ""); // both-error {{not an integral constant expression}} \
                                // both-note {{read of member 'b' of union with active member 'a'}}


  struct F {int x; int y; };
  union { F a; int b; } constexpr u3{};
  static_assert(u3.a.x == 0, "");

  union U4 { F a; int b; } constexpr u4 = U4();
  static_assert(u4.a.x == 0, "");

  union { int a[5]; int b; } constexpr u5{};
  static_assert(u5.a[0] == 0, "");
  static_assert(u5.a[4] == 0, "");
  static_assert(u5.b == 0, ""); // both-error {{not an integral constant expression}} \
                                // both-note {{read of member 'b' of union with active member 'a'}}

  union U6 { int a[5]; int b; } constexpr u6 = U6();
  static_assert(u6.a[0] == 0, "");
  static_assert(u6.a[4] == 0, "");
  static_assert(u6.b == 0, ""); // both-error {{not an integral constant expression}} \
                                // both-note {{read of member 'b' of union with active member 'a'}}

  union UnionWithUnnamedBitfield {
    int : 3;
    int n;
  };
  static_assert(UnionWithUnnamedBitfield().n == 0, "");
  static_assert(UnionWithUnnamedBitfield{}.n == 0, "");
  static_assert(UnionWithUnnamedBitfield{1}.n == 1, "");
}

namespace IndirectField {
  struct S {
    struct {
      union {
        struct {
          int a;
          int b;
        };
        int c;
      };
      int d;
    };
    union {
      int e;
      int f;
    };
    constexpr S(int a, int b, int d, int e) : a(a), b(b), d(d), e(e) {}
    constexpr S(int c, int d, int f) : c(c), d(d), f(f) {}
  };

  constexpr S s1(1,2,3,4);
  constexpr S s2(5, 6, 7);

  static_assert(s1.a == 1, "");
  static_assert(s1.b == 2, "");

  static_assert(s1.c == 0, ""); // both-error {{constant expression}} both-note {{union with active member}}
  static_assert(s1.d == 3, "");
  static_assert(s1.e == 4, "");
  static_assert(s1.f == 0, ""); // both-error {{constant expression}} both-note {{union with active member}}

  static_assert(s2.a == 0, ""); // both-error {{constant expression}} both-note {{union with active member}}
  static_assert(s2.b == 0, ""); // both-error {{constant expression}} both-note {{union with active member}}
  static_assert(s2.c == 5, "");
  static_assert(s2.d == 6, "");
  static_assert(s2.e == 0, ""); // both-error {{constant expression}} both-note {{union with active member}}
  static_assert(s2.f == 7, "");
}

namespace CopyCtor {
  union U {
    int a;
    int b;
  };

  constexpr U x = {42};
  constexpr U y = x;
  static_assert(y.a == 42, "");
  static_assert(y.b == 42, ""); // both-error {{constant expression}} \
                                // both-note {{'b' of union with active member 'a'}}
}

namespace UnionInBase {
  struct Base {
    int y; // both-note {{subobject declared here}}
  };
  struct A : Base {
    int x;
    int arr[3];
    union { int p, q; };
  };
  union B {
    A a;
    int b;
  };
  constexpr int read_wrong_member_indirect() { // both-error {{never produces a constant}}
    B b = {.b = 1};
    int *p = &b.a.y;
    return *p; // both-note 2{{read of member 'a' of union with active member 'b'}}

  }
  static_assert(read_wrong_member_indirect() == 1); // both-error {{not an integral constant expression}} \
                                                    // both-note {{in call to}}
  constexpr int read_uninitialized() {
    B b = {.b = 1};
    int *p = &b.a.y;
    b.a.x = 1;
    return *p; // both-note {{read of uninitialized object}}
  }
  static_assert(read_uninitialized() == 0); // both-error {{constant}} \
                                            // both-note {{in call}}
  constexpr int write_uninitialized() {
    B b = {.b = 1};
    int *p = &b.a.y;
    b.a.x = 1;
    *p = 1;
    return *p;
  }

  constexpr B return_uninit() {
    B b = {.b = 1};
    b.a.x = 2;
    return b;
  }
  constexpr B uninit = return_uninit(); // both-error {{constant expression}} \
                                        // both-note {{subobject 'y' is not initialized}}
  static_assert(return_uninit().a.x == 2);
}

namespace One {
  struct A { long x; };

  union U;
  constexpr A foo(U *up);
  union U {
    A a = foo(this); // both-note {{in call to 'foo(&u)'}}
    int y;
  };

  constexpr A foo(U *up) {
    return {up->y}; // both-note {{read of member 'y' of union}}
  }

  constinit U u = {}; // both-error {{constant init}} \
                      // both-note {{constinit}}
}

namespace CopyAssign {
  union A {
    int a;
    int b;
  };

  constexpr int f() {
    A a{12};
    A b{13};

    b.b = 32;
    b = a ;
    return b.a;
  }
  static_assert(f()== 12);


  constexpr int f2() {
    A a{12};
    A b{13};

    b.b = 32;
    b = a ;
    return b.b; // both-note {{read of member 'b' of union with active member 'a'}}
  }
  static_assert(f2() == 12); // both-error {{not an integral constant expression}} \
                             // both-note {{in call to}}
}

namespace MoveAssign {
  union A {
    int a;
    int b;
  };

  constexpr int f() {
    A b{13};

    b = A{12} ;
    return b.a;
  }
  static_assert(f()== 12);
}

namespace IFD {
  template <class T>
  struct Optional {
    struct {
      union {
        char null_state;
        T val;
      };
    };
    constexpr Optional() : null_state(){}
  };

  constexpr bool test()
  {
    Optional<int> opt{};
    Optional<int> opt2{};
    opt = opt2;
    return true;
  }
  static_assert(test());
}

namespace AnonymousUnion {
  struct A {
    int x;
    union { int p, q; };
  };
  union B {
    A a;
    int bb;
  };

  constexpr B return_init_all() {
    B b = {.bb = 1};
    b.a.x = 2;
    return b;
  }
  static_assert(return_init_all().a.p == 7); // both-error {{}} \
                                             // both-note {{read of member 'p' of union with no active member}}
}

namespace MemberCalls {
  struct S {
    constexpr bool foo() const { return true; }
  };

  constexpr bool foo() { // both-error {{never produces a constant expression}}
    union {
      int a;
      S s;
    } u;

    u.a = 10;
    return u.s.foo(); // both-note 2{{member call on member 's' of union with active member 'a'}}
  }
  static_assert(foo()); // both-error {{not an integral constant expression}} \
                        // both-note {{in call to}}
}

namespace InactiveDestroy {
  struct A {
    constexpr ~A() {}
  };
  union U {
    A a;
    constexpr ~U() {
    }
  };

  constexpr bool foo() { // both-error {{never produces a constant expression}}
    U u;
    u.a.~A(); // both-note 2{{destruction of member 'a' of union with no active member}}
    return true;
  }
  static_assert(foo()); // both-error {{not an integral constant expression}} \
                        // both-note {{in call to}}
}

namespace InactiveTrivialDestroy {
  struct A {};
  union U {
    A a;
  };

  constexpr bool foo() { // both-error {{never produces a constant expression}}
    U u;
    u.a.~A(); // both-note 2{{destruction of member 'a' of union with no active member}}
    return true;
  }
  static_assert(foo()); // both-error {{not an integral constant expression}} \
                        // both-note {{in call to}}
}

namespace ActiveDestroy {
  struct A {};
  union U {
    A a;
  };
  constexpr bool foo2() {
    U u{};
    u.a.~A();
    return true;
  }
  static_assert(foo2());
}

namespace MoveOrAssignOp {
  struct min_pointer {
    int *ptr_;
    constexpr min_pointer(int *p) : ptr_(p) {}
    min_pointer() = default;
  };

  class F {
    struct __long {
      min_pointer __data_;
    };
    union __rep {
      int __s;
      __long __l;
    } __rep_;

  public:
    constexpr F() {
      __rep_ = __rep();
      __rep_.__l.__data_ = nullptr;
    }
  };

  constexpr bool foo() {
    F f{};
    return true;
  }
  static_assert(foo());
}

namespace CopyEmptyUnion {
  struct A {
    union {}; // both-warning {{declaration does not declare anything}}
  };
  constexpr int foo() {
     A a;
     A a2 = a;
     return 1;
  }
  static_assert(foo() == 1);
}

namespace BitFields {
  constexpr bool simple() {
    union U {
      unsigned a : 1;
      unsigned b : 1;
    };

    U u{1};
    u.b = 1;
    return u.b;
  }
  static_assert(simple());
}

namespace deactivateRecurses {

  constexpr int foo() {
    struct A {
      struct {
        int a;
      };
      int b;
    };
    struct B {
      struct {
        int a;
        int b;
      };
    };

    union U {
      A a;
      B b;
    } u;

    u.b.a = 10;
    ++u.b.a;

    u.a.a = 10;
    ++u.a.a;

    if (__builtin_constant_p(u.b.a))
      return 10;

    return 1;
  }
  static_assert(foo() == 1);
}

namespace AnonymousUnion {
  struct Long {
    struct {
      unsigned is_long;
    };
    unsigned Size;
  };

  struct Short {
    struct {
      unsigned is_long;
      unsigned Size;
    };
    char data;
  };

  union Rep {
    Short S;
    Long L;
  };

#define assert_active(F)   if (!__builtin_is_within_lifetime(&F)) (1/0);
#define assert_inactive(F) if ( __builtin_is_within_lifetime(&F)) (1/0);
  consteval int test() {
    union UU {
      struct {
        Rep R;
        int a;
      };
    } U;

    U.R.S.Size = 10;
    assert_active(U);
    assert_active(U.R);
    assert_active(U.R.S);
    assert_active(U.R.S.Size);

    U.a = 10;
    assert_active(U.a);
    assert_active(U);

    assert_active(U);
    assert_active(U.R);
    assert_active(U.R.S);
    assert_active(U.R.S.Size);

    return 1;
  }
  static_assert(test() == 1);
}
#endif

namespace AddressComparison {
  union {
    int a;
    int c;
  } U;
  static_assert(__builtin_addressof(U.a) == (void*)__builtin_addressof(U.c));
  static_assert(&U.a == &U.c);


  struct {
    union {
      struct {
        int a;
        int b;
      } a;
      struct {
        int b;
        int a;
      }b;
    } u;
    int b;
  } S;

  static_assert(&S.u.a.a == &S.u.b.b);
  static_assert(&S.u.a.b != &S.u.b.b);
  static_assert(&S.u.a.b == &S.u.b.b); // both-error {{failed}}


  union {
    int a[2];
    int b[2];
  } U2;

  static_assert(&U2.a[0] == &U2.b[0]);
  static_assert(&U2.a[0] != &U2.b[1]);
  static_assert(&U2.a[0] == &U2.b[1]); // both-error {{failed}}
}
