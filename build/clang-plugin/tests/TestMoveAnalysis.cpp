#include <setjmp.h>

#define MOZ_RENEWS_THIS __attribute__((annotate("moz_renews_this")))
#define MOZ_MOVES_THIS __attribute__((annotate("moz_moves_this")))
#define MOZ_RENEWS_ARG(N) __attribute__((annotate("moz_renews_arg_" #N)))
#define MOZ_MOVES_ARG(N) __attribute__((annotate("moz_moves_arg_" #N)))

// A copy of the Move() function from mfbt

template<typename T>
struct RemoveReference
{
  typedef T Type;
};
template<typename T>
struct RemoveReference<T&>
{
  typedef T Type;
};

template<typename T>
struct RemoveReference<T&&>
{
  typedef T Type;
};

namespace mozilla {
/**
 * Identical to std::Move(); this is necessary until our stlport supports
 * std::move().
 */
template<typename T>
inline typename RemoveReference<T>::Type&&
Move(T&& aX)
{
  return static_cast<typename RemoveReference<T>::Type&&>(aX);
}
}

namespace std {
template<typename T>
inline typename RemoveReference<T>::Type&&
move(T&& aX)
{
  return static_cast<typename RemoveReference<T>::Type&&>(aX);
}
}

using namespace mozilla;

// A shell which acts a bit like nsRefPtr
struct Movable {
  Movable& MOZ_RENEWS_THIS operator=(const Movable& Other) { return *this; }
  void MOZ_MOVES_THIS forget() {}
};

template<class T>
void MOZ_RENEWS_ARG(0) Renew(T &t, const T &u) {}

void gobble(const Movable&& t) {}
void gobble(const Movable& t) {}

void gobbleConstRef(const Movable& t) {}

void f1() {
  Movable m;

  gobble(Move(m)); // expected-note {{Value was moved here}}
  gobble(Move(m)); // expected-error {{Use of moved value}}

  m = Movable();
  gobble(Move(m)); // expected-note {{Value was moved here}}
  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  gobble(std::move(m)); // expected-note {{Value was moved here}}
  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  // Calling Move() to pass an argument to a function with no && overload
  // will not create any errors, as the value isn't actually moved.
  gobbleConstRef(Move(m));
  gobbleConstRef(Move(m));

  m = Movable();
  gobble(m);
  gobble(m);
  gobble(m);

  m = Movable();
  m.forget(); // expected-note {{Value was moved here}}
  m.forget(); // expected-error {{Use of moved value}}

  m = Movable();
  if (true) {
    gobble(m);
  } else {
    m.forget(); // expected-note {{Value was moved here}}
  }

  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  if (true) {
    m.forget(); // expected-note {{Value was moved here}}
  } else {
    m.forget(); // XXX - no note produced in this half of the branch!
  }

  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  switch (1) {
  case 0:
    m.forget(); // expected-note {{Value was moved here}}
    break;
  case 1:
    gobble(m);
    break;
  }
  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  switch (1) {
  case 0:
    m.forget(); // expected-note 2 {{Value was moved here}}
  case 1:
    gobble(m); // expected-error {{Use of moved value}}
    break;
  }
  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  switch (1) {
  case 0:
    m.forget(); // expected-note {{Value was moved here}}
    while (true) {
      break;
    }
    m.forget(); // expected-error {{Use of moved value}} expected-note 2 {{Value was moved here}}
  case 1:
    gobble(m); // expected-error {{Use of moved value}}
    break;
  }
  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  if (true) {
    m.forget();
    return;
  }
  gobble(m);

  m = Movable();
  if (true) {
    m.forget(); // expected-note {{Value was moved here}}
  }

  gobble(m); // expected-error {{Use of moved value}}

  m = Movable();
  Movable a;
  a = m;

  m = Movable();
  m.forget(); // expected-note {{Value was moved here}}
  Movable b;
  b = m; // expected-error {{Use of moved value}}

  m = Movable();
  m.forget();
  Renew(m, b);
  m.forget();

  m = Movable();
  b = Movable();
  m.forget();
  b.forget(); // expected-note 2 {{Value was moved here}}
  Renew(m, b); // expected-error {{Use of moved value}}
  m.forget();
  b.forget(); // expected-error {{Use of moved value}}

  m = Movable();
  m.forget(), m.forget(); // expected-note {{Value was moved here}} expected-error {{Use of moved value}}

  m = Movable();

  while (true) {
    if (false) {
      m.forget(); // expected-note 2 {{Value was moved here}} expected-error {{Use of moved value}}
      continue;
    }
    gobble(m); // expected-error {{Use of moved value}}
  }

  while (true) {
    Movable m2;
    m2.forget();
  }


  while (true) {
    Movable newExpr;
    if (false) {
      if (true) {
        continue;
      }
      newExpr = Movable();
    }
    if (true) {
      m = Move(newExpr);
    } else {
      return newExpr.forget();
    }
  }
}

void f2() {
  Movable m;
foo:
  gobble(Move(m)); // expected-error {{Use of moved value}} expected-note {{Value was moved here}}

  goto foo;
}

void f3() {
  Movable m;

  goto foo;
 foo:
  m = Movable();
}

void f4() {
  Movable m;
  gobble(Move(m));

  jmp_buf env;
  int i = setjmp(env);
  if (i == 0) {
    longjmp(env, 101); // expected-error {{Cannot reason about state of 1 moved value(s) after call to longjmp}}
  }
}

void f5() {
  Movable m;

  jmp_buf env;
  int i = setjmp(env);
  if (i == 0) {
    longjmp(env, 101);
  }
}

void f6() {
  {
    Movable m;

    gobble(Move(m));
  }

  goto foo;
foo:
  Movable m2;
}

int Dispatch(Movable&& event)
{
  return false ? 1 : (gobble(Move(event)), 5);
}
