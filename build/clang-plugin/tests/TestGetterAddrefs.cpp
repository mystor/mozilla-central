#define MOZ_GETTER_ADDREFS_TYPE __attribute__((annotate("moz_getter_addrefs_type")))
#define MOZ_NON_OUTPARAM __attribute__((annotate("moz_non_outparam")))
#define MOZ_DOES_NOT_ADDREF __attribute__((annotate("moz_does_not_addref")))

template<class T>
class DummyPtr {
public:
  DummyPtr();
};

template<class T>
class MOZ_GETTER_ADDREFS_TYPE GetterAddrefsImpl
{
public:
  explicit GetterAddrefsImpl(DummyPtr<T>& aDummyPtr);
  operator T**();
};

template<class T>
GetterAddrefsImpl<T>
getter_AddRefs(DummyPtr<T>& aDummyPtr)
{
  return GetterAddrefsImpl<T>(aDummyPtr);
}

class RCAble
{
public:
  void AddRef();
  void Release();
};

template<class T>
class AddrefUnawareType {
public:
  void CallOnPointerToT(T* aPointer) {}
};

void
CallWithGetterAddrefs(RCAble** aOutParam);

void
NotOutparamType(RCAble** MOZ_NON_OUTPARAM aNonOutparam);

void
DoesNotAddref(RCAble** MOZ_DOES_NOT_ADDREF aNonOutparam);

void
TakesPointerByRef(RCAble**& aStarStarByRef);

RCAble* sStaticPtr;

void f() {
  RCAble* rawPtr;
  DummyPtr<RCAble> smartPtr;
  RCAble** localStarStar;

  CallWithGetterAddrefs(&rawPtr); // expected-error {{This outparameter of type 'RCAble **' must either be passed getter_AddRefs or a forwarded parameter.}} expected-note {{The result will be AddRefed by the getter. This call may leak.}}
  CallWithGetterAddrefs(getter_AddRefs(smartPtr));
  CallWithGetterAddrefs(nullptr);
  CallWithGetterAddrefs(&sStaticPtr);
  CallWithGetterAddrefs(localStarStar); // expected-error {{This outparameter of type 'RCAble **' must either be passed getter_AddRefs or a forwarded parameter.}} expected-note {{The result will be AddRefed by the getter. This call may leak.}}

  AddrefUnawareType<RCAble*>().CallOnPointerToT(&rawPtr);

  NotOutparamType(&rawPtr);
  NotOutparamType(getter_AddRefs(smartPtr)); // XXX: Maybe error here? (bug 1360693)
  NotOutparamType(nullptr);
  NotOutparamType(&sStaticPtr);
  NotOutparamType(localStarStar);

  DoesNotAddref(&rawPtr);
  DoesNotAddref(getter_AddRefs(smartPtr)); // XXX: Maybe error here? (bug 1360693)
  DoesNotAddref(nullptr);
  DoesNotAddref(&sStaticPtr);
  DoesNotAddref(localStarStar);

  TakesPointerByRef(localStarStar);
}

void forwardGetterAddrefs(RCAble** aOutParam) {
  CallWithGetterAddrefs(aOutParam);
  NotOutparamType(aOutParam); // XXX: Maybe error here? (bug 1360693)
  DoesNotAddref(aOutParam); // XXX: Maybe error here? (bug 1360693)
}

