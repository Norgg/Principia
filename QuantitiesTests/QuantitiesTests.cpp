﻿#include "stdafx.h"
#include "CppUnitTest.h"

#include "..\Quantities\Quantities.h"
#include "..\Quantities\NamedQuantities.h"
#include "..\Quantities\SI.h"
#include "..\Quantities\Constants.h"
#include "..\Quantities\Astronomy.h"
#include <stdio.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Principia;
using namespace Principia::Quantities;
using namespace Principia::Constants;
using namespace Principia::SI;
using namespace Principia::Astronomy;

namespace QuantitiesTests {

// TODO(robin): move all these utilities somewhere else.

// The Microsoft equivalent only takes a wchar_t*.
void Log(std::wstring const& message) {
  Logger::WriteMessage(message.c_str());
}
void NewLine() {
  Logger::WriteMessage(L"\n");
}
void LogLine(std::wstring const& message) {
  Log(message);
  NewLine();
}
// The Microsoft equivalent only takes a wchar_t*.
void Assert(bool const test, std::wstring const& message = L"") {
  Assert::IsTrue(test, message.c_str());
}

// The Microsoft equivalent supports errors only for double.
template<typename ValueType, typename ErrorType>
void AssertEqualWithin(ValueType const& left,
                       ValueType const& right,
                       ErrorType const& ε) {
  std::wstring message = L"Should be equal within " + ToString(ε, 3) +
                         L": " + ToString(left) + L" and " + ToString(right) +
                         L".";
  LogLine(message);
  Assert(left == right || Abs(left / right - 1) < ε, message);
  LogLine(L"> Passed!");
}

template<typename ValueType, typename ErrorType>
void AssertNotEqualWithin(ValueType const& left,
                          ValueType const& right,
                          ErrorType const& ε) {
  std::wstring message = L"Should differ by more than " + ToString(ε, 3) +
                         L": " + ToString(left) + L" and " + ToString(right) +
                         L".";
  LogLine(message);
  Assert(Abs(left / right - 1) > ε, message);
  LogLine(L"> Passed!");
}

template<typename D>
void AssertEqual(Quantity<D> const& left,
                 Quantity<D> const& right,
                 Dimensionless const& ε = 1e-15) {
  AssertEqualWithin(left, right, ε);
}

template<typename D>
void AssertNotEqual(Quantity<D> const& left,
                    Quantity<D> const& right,
                    Dimensionless const& ε = 1e-15) {
  AssertNotEqualWithin(left, right, ε);
}

void AssertEqual(Dimensionless const& left,
                 Dimensionless const& right,
                 Dimensionless const& ε = 1e-15) {
  AssertEqualWithin(left, right, ε);
}

void AssertNotEqual(Dimensionless const& left,
                    Dimensionless const& right,
                    Dimensionless const& ε = 1e-15) {
  AssertNotEqualWithin(left, right, ε);
}

template<typename T>
void TestEquality(T const& low, T const& high) {
  LogLine(L"Testing equality on " + ToString(low) + L" ≠ " +
          ToString(high) + L"...");
  Assert(low == low, L"low == low was false.");
  Assert(high == high, L"high == high was false.");
  Assert(high != low, L"high != low was false.");
  Assert(low != high, L"low != high was false.");

  LogLine(L"> True comparisons passed!");

  Assert(!(high == low), L"high == low was true.");
  Assert(!(low == high), L"low == high was true.");
  Assert(!(low != low), L"low != low was true.");
  Assert(!(high != high), L"high != high was true.");

  LogLine(L"> False comparisons passed!");
}

template<typename T>
void TestOrder(T const& low, T const& high) {
  TestEquality(low, high);

  LogLine(L"Testing ordering of " + ToString(low) + L" < " +
          ToString(high) + L"...");
  Assert(high > low, L"high > low was false.");
  Assert(low < high, L"low < high was false.");
  Assert(low >= low, L"low >= low was false.");
  Assert(low <= low, L"low <= low was false.");
  Assert(high >= high, L"high >= high was false.");
  Assert(high <= high, L"high <= high was false.");
  Assert(high >= low, L"high >= low was false.");
  Assert(low <= high, L"low <= high was false.");

  LogLine(L"> True comparisons passed!");

  Assert(!(low > low), L"low > low was true.");
  Assert(!(low < low), L"low < low was true.");
  Assert(!(high > high), L"high > high was true.");
  Assert(!(high < high), L"high < high was true.");
  Assert(!(low > high), L"low > high was true.");
  Assert(!(high < low), L"high < low was true.");
  Assert(!(low >= high), L"low >= high was true.");
  Assert(!(high <= low), L"high <= low was true.");

  LogLine(L"> False comparisons passed!");
}

template<typename T>
void TestAdditiveGroup(T const& zero, T const& a, T const& b, T const& c) {
  AssertEqual(a + zero, a);
  AssertEqual(zero + b, b);
  AssertEqual(a - a, zero);
  AssertEqual(-a - b, -(a + b));
  AssertEqual((a + b) + c, a + (b + c));
  AssertEqual(a - b - c, a - (b + c));
  AssertEqual(a + b, b + a);
  T accumulator = zero;
  T accumulator += a;
  T accumulator += b;
  T accumulator -= c;
  AssertEqual(accumulator, a + b - c);
}

template<typename Vector, typename Scalar>
void TestVectorSpace(Vector const& nullVector, Vector const& u, Vector const& v,
                     Vector const& w, Scalar const& zero, Scalar const& unit,
                     Scalar const& α, Scalar const& β) {
  TestAdditiveGroup(nullVector, u, v, w);
  AssertEqual((α * β) * v, α * (β * v));
  AssertEqual(unit * w, w);
  AssertEqual(zero * u, nullVector);
  AssertEqual(zero * u, nullVector);
}

TEST_CLASS(QuantitiesTests) {
public:
  TEST_METHOD(DimensionlessComparisons) {
    TestOrder(Dimensionless(0), Dimensionless(1));
    TestOrder(Dimensionless(-1), Dimensionless(0));
    TestOrder(Dimensionless(3), Dimensionless(π));
  }
  TEST_METHOD(DimensionfulComparisons) {
    TestOrder(EarthMass, JupiterMass);
    TestOrder(LightYear, Parsec);
    TestOrder(-SpeedOfLight, SpeedOfLight);
    TestOrder(SpeedOfLight * Day, LightYear);
  }
  TEST_METHOD(DimensionlessOperations) {
    Dimensionless const number = 1729;
    Dimensionless accumulator = 0;
    AssertNotEqual(1, 0);
    for (int i = 1; i < 10; ++i) {
      accumulator += number;
      AssertEqual(accumulator, i * number);
    }
    for (int i = 1; i < 10; ++i) { accumulator -= number; }
    AssertEqual(accumulator, 0);
  }
  TEST_METHOD(DimensionlessExponentiation) {
    Dimensionless const number   = π - 42;
    Dimensionless positivePowers = 1;
    Dimensionless negativePowers = 1;
    AssertEqual(Dimensionless(1), number.Pow<0>());
    for (int i = 1; i < 10; ++i) {
      positivePowers *= number;
      negativePowers /= number;
      AssertEqual(number.Pow(i), positivePowers);
      AssertEqual(number.Pow(-i), negativePowers);
    }
  }
  TEST_METHOD(Formatting) {
    auto const allTheUnits = 1 * Metre * Kilogram * Second * Ampere * Kelvin /
                             (Mole * Candela * Cycle * Radian * Steradian);
    std::wstring const expected = std::wstring(L"1e+000 m^1 kg^1 s^1") + 
                                  L" A^1 K^1 mol^-1 cd^-1 cycle^-1 rad^-1" +
                                  L" sr^-1";
    std::wstring const actual = ToString(allTheUnits, 0);
    Assert(actual.compare(expected) == 0);
    std::wstring π16 = L"3.1415926535897931e+000";
    Assert(ToString(π).compare(π16) == 0);
  }
  TEST_METHOD(PhysicalConstants) {
    AssertEqual(1 / SpeedOfLight.Pow<2>(),
                VacuumPermittivity * VacuumPermeability);
    // The Keplerian approximation for the mass of the Sun
    // is fairly accurate.
    AssertEqual(4 * π.Pow<2>() * AstronomicalUnit.Pow<3>() /
                 (GravitationalConstant * JulianYear.Pow<2>()),
               SolarMass, 1e-4);
    AssertEqual(1 * Parsec, 3.26156 * LightYear, 1e-5);
    // The Keplerian approximation for the mass of the Earth
    // is pretty bad, but the error is still only 1%.
    AssertEqual(4 * π.Pow<2>() * LunarDistance.Pow<3>() /
                (GravitationalConstant * (27.321582 * Day).Pow<2>()),
                EarthMass, 1e-2);
    AssertEqual(1 * SolarMass, 1047 * JupiterMass, 1e-3);
    // Delambre & Méchain.
    AssertEqual(GravitationalConstant * EarthMass /
                  (40 * Mega(Metre) / (2 * π)).Pow<2>(),
                StandardGravity, 1e-2);
  }
};
}
