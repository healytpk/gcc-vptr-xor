// <bits/chimeric_ptr.h> -*- C++ -*-
//
// Experimental chimeric pointer type.
//
// This file is part of the GNU ISO C++ Library.
// Copyright (C) 2025 Free Software Foundation, Inc.
//
// This file is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// This file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma GCC system_header

#if __cplusplus >= 202302L

#ifdef _GLIBCXX_INVOKE_H_CHIMERIC_PTR_H      // to implement 'invoke' for 'chimeric_ptr'
#undef _GLIBCXX_INVOKE_H_CHIMERIC_PTR_H

// Forward declaration
template<class... InterfacesAndOptions> class chimeric_ptr;

// Call invoke on chimeric_ptr<Ts...> &
template<typename R, typename MemFun, typename... Ts, typename... Params>
constexpr R __invoke_impl( __invoke_memfun_ref, MemFun&& f, chimeric_ptr<Ts...> &t, Params&&... args )
{
  return __invfwd< chimeric_ptr<Ts...> & >(t).__chimeric_invoke( f, forward<Params>(args)... );
}

// Call invoke on chimeric_ptr<Ts...> const &
template<typename R, typename MemFun, typename... Ts, typename... Params>
constexpr R __invoke_impl( __invoke_memfun_ref, MemFun&& f, chimeric_ptr<Ts...> const &t, Params&&... args )
{
  return __invfwd< chimeric_ptr<Ts...> const & >(t).__chimeric_invoke( f, forward<Params>(args)... );
}

// Call invoke on chimeric_ptr<Ts...> &&
template<typename R, typename MemFun, typename... Ts, typename... Params>
constexpr R __invoke_impl( __invoke_memfun_ref, MemFun&& f, chimeric_ptr<Ts...> &&t, Params&&... args )
{
  return __invfwd< chimeric_ptr<Ts...> && >(t).__chimeric_invoke( f, forward<Params>(args)... );
}

#elif !defined(_GLIBCXX_CHIMERIC_PTR_H)
#define _GLIBCXX_CHIMERIC_PTR_H 1

#include <cassert>                  // assert
#include <cstddef>                  // nullptr_t, size_t
#include <tuple>                    // get, tuple, tuple_element, tuple_size
#include <type_traits>
#include <utility>                  // declval, index_sequence, make_index_sequence, to_underlying

namespace std _GLIBCXX_VISIBILITY(default) {
_GLIBCXX_BEGIN_NAMESPACE_VERSION

namespace chimeric_detail {

  // Check if Derived inherits from Base,
  // and that the CV-qualifiers are compatible
  // for a conversion.
  template<class Derived, class Base>
  concept derived_fromCV = is_base_of_v<Base, Derived> && is_convertible_v<Derived*, Base*>;

  template<class To, class From>
  requires is_lvalue_reference_v<To> && (!is_reference_v<From>)
           && derived_fromCV< From, remove_reference_t<To> >
  [[nodiscard]] constexpr To static_upcast(From &arg) noexcept
  {
    return static_cast<To>(arg);
  }

  template<class ToRef, class From>
  consteval bool user_conversion_detail_noexcept_v(void)
  {
    typedef remove_cvref_t    <ToRef> To  ;
    typedef remove_reference_t<ToRef> ToCV;

    if constexpr (requires(From &f) { f.operator ToCV&(); })  // Prefer exact match first (matches C++ "best viable" more closely)
      return noexcept(declval<From&>().operator ToCV&());
    else if constexpr (requires(From &f) { f.operator To&(); })
      return noexcept(declval<From&>().operator To&());
    else if constexpr (requires(From &f) { f.operator To const&(); } && is_convertible_v<To const*, ToCV*>)
      return noexcept(declval<From&>().operator To const&());
    else if constexpr (requires(From &f) { f.operator To volatile&(); } && is_convertible_v<To volatile*, ToCV*>)
      return noexcept(declval<From&>().operator To volatile&());
    else if constexpr (requires(From &f) { f.operator To const volatile&(); } && is_convertible_v<To const volatile*, ToCV*>)
      return noexcept(declval<From&>().operator To const volatile&());
    else
      return false; // constraints should prevent this from being reached
  }

  template<class ToRef, class From>
  requires   is_lvalue_reference_v<ToRef> &&
           (!is_reference_v<From>) &&
           (!is_base_of_v< remove_cvref_t<ToRef>, remove_cv_t<From> >) &&
           (
              requires { declval<From&>().operator remove_cvref_t<ToRef>               &(); } ||
             (requires { declval<From&>().operator remove_cvref_t<ToRef> const         &(); } && is_convertible_v< remove_cvref_t<ToRef> const         *, remove_reference_t<ToRef>* >) ||
             (requires { declval<From&>().operator remove_cvref_t<ToRef>       volatile&(); } && is_convertible_v< remove_cvref_t<ToRef>       volatile*, remove_reference_t<ToRef>* >) ||
             (requires { declval<From&>().operator remove_cvref_t<ToRef> const volatile&(); } && is_convertible_v< remove_cvref_t<ToRef> const volatile*, remove_reference_t<ToRef>* >)
           )
  constexpr ToRef user_conversion(From &arg) noexcept( user_conversion_detail_noexcept_v<ToRef,From>() )
  {
    typedef remove_cvref_t    <ToRef> To  ;
    typedef remove_reference_t<ToRef> ToCV;

    if constexpr (requires { arg.operator ToCV&(); })  // Prefer exact match first (matches C++ "best viable" more closely)
      return arg.operator ToCV&();
    else if constexpr (requires { arg.operator To&(); })
      return arg.operator To&();
    else if constexpr (requires { arg.operator To const&(); } && is_convertible_v<To const*, ToCV*>)
      return arg.operator To const&();
    else if constexpr (requires { arg.operator To volatile&(); } && is_convertible_v<To volatile*, ToCV*>)
      return arg.operator To volatile&();
    else if constexpr (requires { arg.operator To const volatile&(); } && is_convertible_v<To const volatile*, ToCV*>)
      return arg.operator To const volatile&();
    else
      static_assert(false, "control should never reach here -- prevented by constraints");
  }

  template <typename T>
  concept option = requires { typename T::__tag_chimera_option; };

  // Canonicalise for duplicate checks but ignore const/volatile
  template<class T, class... Us>
  struct none_same_canon
    : bool_constant< (!is_same_v< remove_cv_t<T>, remove_cv_t<Us> > && ...) > {};

  template<class... Ts>
  struct unique_canon : true_type {};

  template<class T0, class... TRest>
  struct unique_canon<T0, TRest...>
    : bool_constant< none_same_canon<T0, TRest...>::value
                     && unique_canon<TRest...>::value > {};

  template<class... Ts>
  struct has_no_options
    : bool_constant< (!option<Ts> && ...) > {};

  // Enforce: All option types must precede all interface types
  template<class... Ts>
  struct options_then_interfaces : true_type {};

  template<class T0, class... TRest>
  struct options_then_interfaces<T0, TRest...>
    : bool_constant<
      option<T0>
      ? options_then_interfaces<TRest...>::value
      : has_no_options<TRest...>::value > {};

  // Convert parameter pack to tuple
  template<class T, class Tuple>
  struct tuple_prepend;
  template<class T, class... Ts>
  struct tuple_prepend< T, tuple<Ts...> >
  { using type = tuple<T, Ts...>; };

  // Filter interfaces out of parameter pack
  template<class... InterfacesAndOptions>
  struct filter_interfaces;
  template<> struct filter_interfaces<> { using type = tuple<>; };
  template<class I0, class... Rest>
  struct filter_interfaces<I0, Rest...>
  {
    using tail = typename filter_interfaces<Rest...>::type;
    using type = conditional_t<
      option<I0>,
      tail,
      typename tuple_prepend<I0, tail>::type >;
  };

  // Filter options out of parameter pack
  template<class... InterfacesAndOptions>
  struct filter_options;
  template<> struct filter_options<> { using type = tuple<>; };
  template<class I0, class... Rest>
  struct filter_options<I0, Rest...>
  {
    using tail = typename filter_options<Rest...>::type;
    using type = conditional_t<
      option<I0>,
      typename tuple_prepend<I0, tail>::type,
      tail>;
  };

  // Convert tuple into tuple of pointers
  template<class Tuple>
  struct tuple_pointerize;
  template<class... Ts>
  struct tuple_pointerize< tuple<Ts...> >
  { using type = tuple<Ts*...>; };

  template<class B, class Tuple>
  struct tuple_contains_type : false_type {};
  template<class B, class... Ts>
  struct tuple_contains_type< B, tuple<Ts...> >
    : bool_constant< (is_same_v<B, Ts> || ...) > {};

  template<class B, class Tuple>
  struct tuple_contains_type_derived_fromCV : false_type {};
  template<class B, class... Ts>
  struct tuple_contains_type_derived_fromCV< B, tuple<Ts...> >
    : bool_constant< (derived_fromCV<Ts, B> || ...) > {};

  // Option tuple must have no const/volatile
  template<class Tuple>
  struct tuple_all_uncv : false_type {};
  template<class... Ts>
  struct tuple_all_uncv< tuple<Ts...> >
    : bool_constant< (is_same_v<Ts, remove_cv_t<Ts> > && ...) > {};

  // For alphabetical order: option types define
  //   typedef integral_constant<unsigned, N> __tag_chimera_option;
  template<class T, class = void>
  struct option_key {};

  template<class T>
  struct option_key< T, void_t< decltype(T::__tag_chimera_option::value) > >
    : integral_constant< unsigned, T::__tag_chimera_option::value > {};

  template<class... Ts>
  struct is_option_pack_alphabetical : true_type {};

  template<class T0, class T1, class... Ts>
  struct is_option_pack_alphabetical<T0, T1, Ts...>
    : bool_constant<
        (option_key<T0>::value < option_key<T1>::value)
        && is_option_pack_alphabetical<T1, Ts...>::value > {};

  template<class Tuple>
  struct is_option_tuple_alphabetical : false_type {};

  template<class... Ts>
  struct is_option_tuple_alphabetical< tuple<Ts...> >
    : is_option_pack_alphabetical<Ts...> {};

  template<class Want, class Tuple>
  struct tuple_find_uncv;

  template<class Want>
  struct tuple_find_uncv<Want, tuple<>>
  { using type = void; };

  template<class Want, class T0, class... Ts>
  struct tuple_find_uncv<Want, tuple<T0, Ts...>>
  {
    using type = conditional_t<
      is_same_v<remove_cv_t<T0>, remove_cv_t<Want>>,
      T0,
      typename tuple_find_uncv<Want, tuple<Ts...>>::type>;
  };

  template<class Want, class Tuple>
  using tuple_find_uncv_t = typename tuple_find_uncv<Want, Tuple>::type;

  template<class I, class RhsTuple>
  concept rhs_supplies_interface =
    (!is_void_v<tuple_find_uncv_t<I, RhsTuple>>)
    && is_convertible_v<tuple_find_uncv_t<I, RhsTuple>*, I*>;

  template<class LhsTuple, class RhsTuple>
  concept rhs_supplies_all =
    []<class... Is>(type_identity<tuple<Is...>>) consteval
    {
      return (rhs_supplies_interface<Is, RhsTuple> && ...);
    }(type_identity<LhsTuple>{});

  // Index of T within tuple<Ts...>.
  template<class T, class Tuple>
  struct tuple_index;

  template<class T, class... Ts>
  struct tuple_index<T, tuple<T, Ts...>> : integral_constant<size_t, 0u> {};

  template<class T, class U, class... Ts>
  struct tuple_index<T, tuple<U, Ts...>>
    : integral_constant<size_t, 1u + tuple_index<T, tuple<Ts...>>::value> {};

} // namespace chimeric_detail

struct allow_dynamic    { using __tag_chimera_option = integral_constant<unsigned, 10u>; };
struct bases_only       { using __tag_chimera_option = integral_constant<unsigned, 20u>; };
struct forbid_ambiguity { using __tag_chimera_option = integral_constant<unsigned, 30u>; };
struct many_pointers    { using __tag_chimera_option = integral_constant<unsigned, 40u>; };

template<class... InterfacesAndOptions>
class chimeric_ptr final {

  typedef int __tag_chimera;

  template<class...> friend class chimeric_ptr;

  static_assert(sizeof...(InterfacesAndOptions) > 0u,
                "std::chimeric_ptr must have at least one template parameter type");

  static_assert((is_class_v<InterfacesAndOptions> && ...),
                "std::chimeric_ptr: all template parameters must be class types (no pointers, no references)");

  static_assert(chimeric_detail::unique_canon<InterfacesAndOptions...>::value,
                "std::chimeric_ptr: duplicate template parameters are not permitted after removing const/volatile");

  static_assert(chimeric_detail::options_then_interfaces<InterfacesAndOptions...>::value,
                "std::chimeric_ptr: all options must precede all interfaces in template parameter list");

  static_assert( !(__is_specialization_of<InterfacesAndOptions,chimeric_ptr> || ...),
                "std::chimeric_ptr cannot be used as a template argument to std::chimeric_ptr");

  typedef typename chimeric_detail::filter_interfaces<InterfacesAndOptions...>::type ChimericInterfacesTuple_t;
  typedef typename chimeric_detail::filter_options   <InterfacesAndOptions...>::type ChimericOptionsTuple_t   ;

  typedef typename chimeric_detail::tuple_pointerize<ChimericInterfacesTuple_t>::type ChimericInterfacePointersTuple_t;

  static_assert(tuple_size<ChimericInterfacesTuple_t>::value > 0u,
                "std::chimeric_ptr must have at least one interface");

  static_assert(chimeric_detail::tuple_all_uncv<ChimericOptionsTuple_t>::value,
                "std::chimeric_ptr: option types must not be const/volatile");

  static_assert(chimeric_detail::is_option_tuple_alphabetical<ChimericOptionsTuple_t>::value,
                "std::chimeric_ptr: options must be in alphabetical order");

  inline static constexpr bool
     opt_allow_dynamic    = chimeric_detail::tuple_contains_type<allow_dynamic   , ChimericOptionsTuple_t>::value
    ,opt_bases_only       = chimeric_detail::tuple_contains_type<bases_only      , ChimericOptionsTuple_t>::value
    ,opt_forbid_ambiguity = chimeric_detail::tuple_contains_type<forbid_ambiguity, ChimericOptionsTuple_t>::value
    ,opt_many_pointers    = chimeric_detail::tuple_contains_type<many_pointers   , ChimericOptionsTuple_t>::value
  ;

  static_assert(false == (opt_allow_dynamic && opt_bases_only),
                "std::chimeric_ptr: options 'allow_dynamic' and 'bases_only' cannot be combined");

  // Two-pointer representation.
  struct __two_ptrs {
    void const volatile *obj;
    void const volatile * (*get)(void const volatile *, size_t) noexcept;
  };

  conditional_t<opt_many_pointers,
                ChimericInterfacePointersTuple_t,
                __two_ptrs> __chimeric_addresses{}; // start off all nullptr

  enum class HowInterfaceGotten : int {
    not_possible  =  0,
    static_upcast = +1,
    user_conversion_nothrow = +2,
    user_conversion_throw   = -2,
    dynamic = -3,
  };

  template<class T, class Interface>
  static consteval HowInterfaceGotten HowToGet(void) noexcept
  {
    if constexpr ( requires(T &t){ chimeric_detail::static_upcast<Interface&>(t); } )
    {
      return HowInterfaceGotten::static_upcast;
    }
    else if constexpr ( opt_bases_only )
    {
      // set_from will not use conversion operators when 'bases_only' is set
      // (and 'allow_dynamic' cannot be combined with 'bases_only')
      return HowInterfaceGotten::not_possible;
    }
    else if constexpr ( requires(T &t){ chimeric_detail::user_conversion<Interface&>(t); } )
    {
      if constexpr ( noexcept(chimeric_detail::user_conversion<Interface&>(declval<T&>())) )
        return HowInterfaceGotten::user_conversion_nothrow;
      else
        return HowInterfaceGotten::user_conversion_throw;
    }
    else if constexpr ( opt_allow_dynamic && is_polymorphic_v<T>
                        && requires (T &t){ dynamic_cast<Interface&>(t); })
    {
      return HowInterfaceGotten::dynamic;
    }
    else
    {
      return HowInterfaceGotten::not_possible;
    }
  }

  template<typename T, size_t... Is>
  static consteval bool CanAnyThrow( index_sequence<Is...> ) noexcept
  {
    constexpr auto mylambda = []<typename Interface>(void) constexpr noexcept -> bool
      {
        return to_underlying( HowToGet<T,Interface>() ) < 0;
      };

    return ( mylambda.template operator()< tuple_element_t<Is, ChimericInterfacesTuple_t>  >() || ... );
  }

  template<typename T>
  static consteval bool CanAnyThrow(void) noexcept
  {
    return CanAnyThrow<T>(make_index_sequence< tuple_size_v<ChimericInterfacesTuple_t> >{});
  }

  // -------------------------
  // normal dispatcher
  // -------------------------

  template<class T, class Interface>
  static constexpr void const volatile *__get_one(T *const p) noexcept
  {
    // Normal mode is only sound if interface acquisition is non-throwing at access time.
    if constexpr ( requires (T &t) { chimeric_detail::static_upcast<Interface&>(t); } )
    {
      return static_cast<void const volatile *>(
        __builtin_addressof( chimeric_detail::static_upcast<Interface&>(*p) )
      );
    }
    else if constexpr ( !opt_bases_only
                        && requires (T &t) { chimeric_detail::user_conversion<Interface&>(t); }
                        && noexcept( chimeric_detail::user_conversion<Interface&>(declval<T&>()) ) )
    {
      return static_cast<void const volatile *>(
        __builtin_addressof( chimeric_detail::user_conversion<Interface&>(*p) )
      );
    }
    else if constexpr ( opt_allow_dynamic && requires (T &t) { dynamic_cast<Interface&>(t); } )
    {
      static_assert(false,
                    "std::chimeric_ptr: normal mode does not support dynamic interface acquisition "
                    "(it can throw or otherwise fail at access time).");
    }
    else if constexpr ( !opt_bases_only
                        && requires (T &t) { chimeric_detail::user_conversion<Interface&>(t); } )
    {
      static_assert(false,
                    "std::chimeric_ptr: normal mode requires user conversions to be noexcept.");
    }
    else
    {
      static_assert(false,
                    "std::chimeric_ptr: cannot obtain required interface for normal mode.");
    }
    __builtin_unreachable();
  }

  template<class T, size_t... Is>
  static constexpr void const volatile *__dispatch_impl(void const volatile *obj, size_t idx, index_sequence<Is...>) noexcept
  {
    T *const p = const_cast<T*>( static_cast<T const volatile*>(obj) );
    void const volatile *out = nullptr;

    (( idx == Is
         ? (out = __get_one<T, tuple_element_t<Is, ChimericInterfacesTuple_t>>(p), true)
         : false
     ) || ...);

    return out;
  }

  template<class T>
  static constexpr void const volatile *__dispatch(void const volatile *obj, size_t idx) noexcept
  {
    return __dispatch_impl<T>(
      obj, idx,
      make_index_sequence< tuple_size_v<ChimericInterfacesTuple_t> >{}
    );
  }

  // -------------------------
  // tuple-pointer mode builder
  // -------------------------

  template<class T, size_t... Is>
  constexpr void set_from(T *const p, index_sequence<Is...>) noexcept( !CanAnyThrow<T>() )
  {
    constexpr auto mylambda = []<typename Interface>(Interface *&pI, T *const p2) constexpr
      {
        if constexpr ( requires { pI = __builtin_addressof( chimeric_detail::static_upcast<Interface&>(*p2) ); } )
        {
          pI = __builtin_addressof( chimeric_detail::static_upcast<Interface&>(*p2) );
        }
        else if constexpr ( opt_bases_only )
        {
          static_assert(false, "std::chimeric_ptr : cannot get interface because of option 'bases_only'");
        }
        else if constexpr ( requires { pI = __builtin_addressof( chimeric_detail::user_conversion<Interface&>(*p2) ); } )
        {
          pI = __builtin_addressof( chimeric_detail::user_conversion<Interface&>(*p2) );
        }
        else if constexpr ( opt_allow_dynamic )
        {
          if constexpr ( !is_polymorphic_v<T> )
          {
            static_assert(false, "std::chimeric_ptr : cannot get interface because T is not polymorphic (dynamic allowed)");
          }
          else if constexpr ( false == requires { pI = __builtin_addressof( dynamic_cast<Interface&>(*p2) ); } )
          {
            static_assert(false, "std::chimeric_ptr : cannot get interface because dynamic_cast is ill-formed");
          }
          else
          {
            pI = __builtin_addressof( dynamic_cast<Interface&>(*p2) );  // might throw 'std::bad_cast'
          }
        }
        else
        {
          static_assert(false, "std::chimeric_ptr : cannot get interface from supplied object (dynamic disallowed)");
        }
      };

    ( mylambda.template operator()< tuple_element_t<Is, ChimericInterfacesTuple_t>  >( get<Is>(__chimeric_addresses), p ), ...);
  }

  constexpr bool __has_value(void) const noexcept
  {
    if constexpr ( opt_many_pointers )
      return nullptr != get<0u>(__chimeric_addresses);
    else
      return nullptr != __chimeric_addresses.obj;
  }

  constexpr void __reset(void) noexcept
  {
    __chimeric_addresses = {};
  }

  template<class... Ts>
  inline static constexpr bool __compatible_rhs =
    (!std::same_as< chimeric_ptr, chimeric_ptr<Ts...> >)
    && chimeric_detail::rhs_supplies_all<
         ChimericInterfacesTuple_t,
         typename chimeric_ptr<Ts...>::ChimericInterfacesTuple_t>
    // In normal mode with index-dispatch, we cannot safely rebind the dispatcher for a *different*
    // interface list (or different acquisition policy) without extra state. So only allow converting
    // to normal mode when the interface list and key options match.
    && ( opt_many_pointers
         || (   !chimeric_ptr<Ts...>::opt_many_pointers
             && std::same_as<
                  ChimericInterfacesTuple_t,
                  typename chimeric_ptr<Ts...>::ChimericInterfacesTuple_t>
             && (opt_bases_only    == chimeric_ptr<Ts...>::opt_bases_only)
             && (opt_allow_dynamic == chimeric_ptr<Ts...>::opt_allow_dynamic)
            )
       );

  template<class I, class RhsInterfacesTuple, class... Ts>
  static constexpr I *__get_from_rhs(chimeric_ptr<Ts...> const &rhs) noexcept
  {
    using J = chimeric_detail::tuple_find_uncv_t<I, RhsInterfacesTuple>;
    // First get J* from rhs via its public conversion operator, then cv-safe convert to I*.
    return static_cast<I*>( static_cast<J*>(rhs) );
  }

  template<class... Ts>
  constexpr void __copy_ptrs_from(chimeric_ptr<Ts...> const &rhs) noexcept
  {
    typedef typename chimeric_ptr<Ts...>::ChimericInterfacesTuple_t RhsInterfacesTuple;

    if ( !rhs )
    {
      __chimeric_addresses = {};
      return;
    }

    auto const mylambda = [&]<class... Is>(type_identity< tuple<Is...> >) constexpr
    {
      ((get<Is*>(__chimeric_addresses) = __get_from_rhs<Is, RhsInterfacesTuple>(rhs)), ...);
    };

    mylambda( type_identity<ChimericInterfacesTuple_t>{} );
  }

  template<class... Ts>
  constexpr void __copy_from(chimeric_ptr<Ts...> const &rhs) noexcept
  requires __compatible_rhs<Ts...>
  {
    if ( !rhs )
    {
      __chimeric_addresses = {};
      return;
    }

    if constexpr ( opt_many_pointers )
    {
      __copy_ptrs_from(rhs);
    }
    else
    {
      __chimeric_addresses = rhs.__chimeric_addresses;
    }
  }

public:
  constexpr chimeric_ptr(void) noexcept = default;
  constexpr chimeric_ptr(nullptr_t) noexcept {}

  constexpr chimeric_ptr(chimeric_ptr const& ) noexcept = default;
  constexpr chimeric_ptr(chimeric_ptr      &&) noexcept = default;
  constexpr chimeric_ptr &operator=(chimeric_ptr const&) noexcept = default;
  constexpr chimeric_ptr &operator=(chimeric_ptr     &&) noexcept = default;

  template<class T>
  /* implicit */ constexpr chimeric_ptr(T *const p) noexcept( !CanAnyThrow<T>() )
  {
    static_assert( is_class_v<T>, "std::chimeric_ptr : constructor argument must be pointer to class" );
    if ( nullptr == p ) return;

    if constexpr ( opt_many_pointers )
    {
      set_from( p, make_index_sequence< tuple_size_v<ChimericInterfacesTuple_t> >{} );
    }
    else
    {
      // Hard requirement: normal mode cannot defer potentially-throwing acquisition to later.
      static_assert(!CanAnyThrow<T>(),
        "std::chimeric_ptr: normal mode requires that all interface acquisition is non-throwing "
        "(no throwing user conversions and no dynamic path).");

      __chimeric_addresses.obj = static_cast<void const volatile *>(p); // adds cv (ok)
      __chimeric_addresses.get = &__dispatch<T>;
    }

  }

  constexpr chimeric_ptr &operator=(nullptr_t) noexcept
  {
    __chimeric_addresses = {};
    return *this;
  }

  explicit constexpr operator bool(void) const noexcept
  {
    return __has_value();
  }

  constexpr bool operator!(void) const noexcept { return !static_cast<bool>(*this); }

  friend constexpr bool operator==(chimeric_ptr const &p, nullptr_t) noexcept { return  !p; }
  friend constexpr bool operator==(nullptr_t, chimeric_ptr const &p) noexcept { return  !p; }
  friend constexpr bool operator!=(chimeric_ptr const &p, nullptr_t) noexcept { return !!p; }
  friend constexpr bool operator!=(nullptr_t, chimeric_ptr const &p) noexcept { return !!p; }

  template<class B, size_t... Is>
  requires chimeric_detail::tuple_contains_type_derived_fromCV<B, ChimericInterfacesTuple_t>::value
  constexpr B *operator_B_star( index_sequence<Is...> ) const noexcept
  {
    if ( !*this ) return nullptr;

    B *p = nullptr;

    constexpr auto mylambda = []<class T>( decltype(__chimeric_addresses) const &addrs, B *&p2 ) constexpr noexcept -> void
      {
        if constexpr ( chimeric_detail::derived_fromCV< T, B > )
        {
          if ( nullptr != p2 ) return;
          if constexpr ( opt_many_pointers )
          {
            p2 = get<T*>(addrs);  // implict conversion from Derived* to Base*
          }
          else
          {
            constexpr size_t i = chimeric_detail::tuple_index<T, ChimericInterfacesTuple_t>::value;
            void const volatile *const raw = addrs.get(addrs.obj, i);
            p2 = const_cast<T*>( static_cast< remove_cv_t<T> const volatile* >(raw) );  // implict conversion from Derived* to Base*
          }
        }
      };

    ( mylambda.template operator()< tuple_element_t<Is, ChimericInterfacesTuple_t>  >(__chimeric_addresses, p), ... );
    assert( nullptr != p );
    return p;
  }

  template<class B>
  requires chimeric_detail::tuple_contains_type_derived_fromCV<B, ChimericInterfacesTuple_t>::value
  constexpr operator B*(void) const noexcept
  {
    return this->operator_B_star<B>( make_index_sequence< tuple_size_v<ChimericInterfacesTuple_t> >{} );
  }

  // Converting copy-constructor
  template<class... Ts> requires __compatible_rhs<Ts...>
  constexpr chimeric_ptr(chimeric_ptr<Ts...> const &original) noexcept
  {
    __copy_from(original);
  }

  // Converting move-constructor
  template<class... Ts> requires __compatible_rhs<Ts...>
  constexpr chimeric_ptr(chimeric_ptr<Ts...> &&original) noexcept
  {
    __copy_from(original);
    original = nullptr;
  }

  // Converting copy-assignment
  template<class... Ts> requires __compatible_rhs<Ts...>
  constexpr chimeric_ptr &operator=(chimeric_ptr<Ts...> const &rhs) noexcept
  {
    __copy_from(rhs);
    return *this;
  }

  // Converting move-assignment
  template<class... Ts> requires __compatible_rhs<Ts...>
  constexpr chimeric_ptr& operator=(chimeric_ptr<Ts...> &&rhs) noexcept
  {
    __copy_from(rhs);
    rhs = nullptr;
    return *this;
  }

  template<typename> struct __member_function_ptr_class;
  template<typename T, typename Class> struct __member_function_ptr_class< T Class::* > { typedef Class type; };
  template<class MemPtr, class... Params>
  requires is_member_function_pointer_v< remove_cvref_t<MemPtr> >
  constexpr decltype(auto) __chimeric_invoke(MemPtr const mem, Params&&... args) const
    noexcept( noexcept( (declval<typename __member_function_ptr_class< remove_cvref_t<MemPtr> >::type*>()->*mem)( forward<Params>(args)... ) ) )
  {
    typedef typename __member_function_ptr_class< remove_cvref_t<MemPtr> >::type Class;
    Class *const p = static_cast<Class*>( *this );
    return (p->*mem)( forward<Params>(args)... );
  }
};

template<class MemPtr, class... Ts, class... Params>
requires is_member_function_pointer_v< remove_cvref_t<MemPtr> >
         && requires ( chimeric_ptr<Ts...> &t )
            {
              t.__chimeric_invoke( declval<MemPtr>(), declval<Params>()... );
            }
struct __invoke_result< MemPtr, chimeric_ptr<Ts...> &, Params... > {
  typedef __invoke_memfun_ref __invoke_type;
  typedef decltype( declval< chimeric_ptr<Ts...> & >().__chimeric_invoke( declval<MemPtr>(), declval<Params>()... ) ) type;
};

template<class MemPtr, class... Ts, class... Params>
requires is_member_function_pointer_v< remove_cvref_t<MemPtr> >
         && requires ( chimeric_ptr<Ts...> const &t )
            {
              t.__chimeric_invoke( declval<MemPtr>(), declval<Params>()... );
            }
struct __invoke_result< MemPtr, chimeric_ptr<Ts...> const &, Params... > {
  typedef __invoke_memfun_ref __invoke_type;
  typedef decltype( declval< chimeric_ptr<Ts...> const & >().__chimeric_invoke( declval<MemPtr>(), declval<Params>()... ) ) type;
};

template<class MemPtr, class... Ts, class... Params>
requires is_member_function_pointer_v< remove_cvref_t<MemPtr> >
         && requires ( chimeric_ptr<Ts...> &&t )
            {
              t.__chimeric_invoke( declval<MemPtr>(), declval<Params>()... );
            }
struct __invoke_result< MemPtr, chimeric_ptr<Ts...> &&, Params... > {
  typedef __invoke_memfun_ref __invoke_type;
  typedef decltype( declval< chimeric_ptr<Ts...> && >().__chimeric_invoke( declval<MemPtr>(), declval<Params>()... ) ) type;
};

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // _GLIBCXX_INVOKE_H_CHIMERIC_PTR_H || _GLIBCXX_CHIMERIC_PTR_H

#else  // else leg of "if __cplusplus >= 202302L"
#  if 1 != _GLIBCXX_INVOKE_H_CHIMERIC_PTR_H
#    error "std::chimeric_ptr requires C++23 or later."
#  endif
#endif
