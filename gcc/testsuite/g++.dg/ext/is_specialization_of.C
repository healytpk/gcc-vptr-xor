// { dg-do compile }
// { dg-options "-std=gnu++23" }

#include <cstddef>
#include <array>
#include <list>
#include <ratio>
#include <vector>

static_assert( __is_specialization_of(std::array<int, 3>, std::array ));
static_assert( __is_specialization_of(std::list <int   >, std::list  ));
static_assert( __is_specialization_of(std::ratio <1,  2>, std::ratio ));
static_assert( __is_specialization_of(std::vector<int  >, std::vector));

static_assert(!__is_specialization_of(std::array<int, 3>, std::vector));
static_assert(!__is_specialization_of(std::list <int   >, std::array ));
static_assert(!__is_specialization_of(std::ratio<1,   2>, std::array ));
static_assert(!__is_specialization_of(std::array<int, 2>, std::ratio ));

static_assert(!__is_specialization_of(int  , std::array ));
static_assert(!__is_specialization_of(float, std::ratio ));
static_assert(!__is_specialization_of(int  , std::vector));
static_assert(!__is_specialization_of(float, std::list  ));

template<template<typename, std::size_t> class TT>
constexpr bool f()
{
  return __is_specialization_of(std::array<int, 3>, TT);
}

static_assert(f<std::array>());
