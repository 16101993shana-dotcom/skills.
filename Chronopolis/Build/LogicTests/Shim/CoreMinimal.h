// Minimal stand-in for the Unreal Engine core types used by the plain-logic
// files of Chronopolis. For compiling and running that logic outside the engine.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using int8 = std::int8_t; using int16 = std::int16_t; using int32 = std::int32_t; using int64 = std::int64_t;
using uint8 = std::uint8_t; using uint16 = std::uint16_t; using uint32 = std::uint32_t; using uint64 = std::uint64_t;
using TCHAR = char;
#define TEXT(x) x
#define INDEX_NONE (-1)
#define UE_SQRT_2 (1.4142135623730950488016887242097f)
#define UE_KINDA_SMALL_NUMBER (1.e-4f)
#define UE_SMALL_NUMBER (1.e-8f)
#define UE_DOUBLE_KINDA_SMALL_NUMBER (1.e-4)
#define UE_DOUBLE_SMALL_NUMBER (1.e-8)
#define UE_PI (3.1415926535897932f)
#define UE_DOUBLE_PI (3.141592653589793238462643383279502884197169399)
#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UMETA(...)
#define GENERATED_BODY()
#define WITH_DEV_AUTOMATION_TESTS 1
#define check(x) do { if (!(x)) { std::fprintf(stderr, "check failed: %s\n", #x); std::abort(); } } while (0)
enum EForceInit { ForceInit };
#define MoveTemp(x) std::move(x)
template <typename T> void Swap(T& A, T& B) { std::swap(A, B); }

template <typename T> struct TNumericLimits { static T Max() { return std::numeric_limits<T>::max(); } static T Lowest() { return std::numeric_limits<T>::lowest(); } static T Min() { return std::numeric_limits<T>::min(); } };

struct FString {
	std::string S;
	FString() = default; FString(const char* In) : S(In ? In : "") {} FString(std::string In) : S(std::move(In)) {}
	const char* operator*() const { return S.c_str(); }
	bool IsEmpty() const { return S.empty(); }
	bool operator==(const FString& O) const { return S == O.S; }
	FString operator+(const FString& O) const { return FString(S + O.S); }
	static FString Printf(const char* Format, ...) { char Buffer[4096]; va_list Args; va_start(Args, Format); std::vsnprintf(Buffer, sizeof(Buffer), Format, Args); va_end(Args); return FString(Buffer); }
};
struct FName {
	std::string S;
	FName() = default; FName(const char* In) : S(In ? In : "") {}
	FString ToString() const { return FString(S); }
	bool operator==(const FName& O) const { return S == O.S; } bool operator!=(const FName& O) const { return S != O.S; }
	bool operator<(const FName& O) const { return S < O.S; }
};
static const FName NAME_None;
struct FText { FString S; };

struct FMath {
	template <typename A, typename B> static auto Max(A a, B b) -> std::common_type_t<A, B> { return a > b ? a : b; }
	template <typename A, typename B> static auto Min(A a, B b) -> std::common_type_t<A, B> { return a < b ? a : b; }
	template <typename T, typename A, typename B> static auto Clamp(T x, A lo, B hi) -> std::common_type_t<T, A, B> { return x < lo ? lo : (x > hi ? hi : x); }
	template <typename T> static T Abs(T x) { return x < 0 ? -x : x; }
	template <typename T> static T Sqrt(T x) { return std::sqrt(x); }
	template <typename T> static T Sin(T x) { return std::sin(x); }
	template <typename T> static T Cos(T x) { return std::cos(x); }
	template <typename T> static T Acos(T x) { return std::acos(x); }
	template <typename A, typename B> static auto Atan2(A y, B x) { return std::atan2(y, x); }
	template <typename A, typename B> static auto Pow(A a, B b) { return std::pow(a, b); }
	static int32 FloorToInt32(double x) { return static_cast<int32>(std::floor(x)); }
	static int32 CeilToInt32(double x) { return static_cast<int32>(std::ceil(x)); }
	static int32 RoundToInt32(double x) { return static_cast<int32>(std::floor(x + 0.5)); }
	template <typename T, typename U> static T Lerp(const T& A, const T& B, const U& Alpha) { return static_cast<T>(A + (B - A) * Alpha); }
	static bool IsNearlyEqual(double A, double B, double Tol = 1.e-8) { return std::fabs(A - B) <= Tol; }
	static bool IsNearlyZero(double A, double Tol = 1.e-8) { return std::fabs(A) <= Tol; }
	template <typename T> static T DegreesToRadians(T D) { return D * static_cast<T>(UE_DOUBLE_PI / 180.0); }
	template <typename T> static T RadiansToDegrees(T R) { return R * static_cast<T>(180.0 / UE_DOUBLE_PI); }
	static int32 DivideAndRoundUp(int32 A, int32 B) { return (A + B - 1) / B; }
	static double FindDeltaAngleRadians(double A, double B) { double D = B - A; while (D > UE_DOUBLE_PI) D -= 2.0 * UE_DOUBLE_PI; while (D < -UE_DOUBLE_PI) D += 2.0 * UE_DOUBLE_PI; return D; }
	static float PerlinNoise2D(const struct FVector2D& P);
};

struct FVector2D {
	double X = 0.0, Y = 0.0;
	FVector2D() = default; FVector2D(double InX, double InY) : X(InX), Y(InY) {}
	FVector2D operator+(const FVector2D& O) const { return { X + O.X, Y + O.Y }; }
	FVector2D operator-(const FVector2D& O) const { return { X - O.X, Y - O.Y }; }
	FVector2D operator-() const { return { -X, -Y }; }
	FVector2D operator*(double K) const { return { X * K, Y * K }; }
	FVector2D operator/(double K) const { return { X / K, Y / K }; }
	FVector2D& operator+=(const FVector2D& O) { X += O.X; Y += O.Y; return *this; }
	bool operator==(const FVector2D& O) const { return X == O.X && Y == O.Y; }
	double Size() const { return std::sqrt(X * X + Y * Y); }
	double SizeSquared() const { return X * X + Y * Y; }
	bool IsNearlyZero(double Tol = 1.e-4) const { return std::fabs(X) <= Tol && std::fabs(Y) <= Tol; }
	FVector2D GetSafeNormal(double Tol = 1.e-8) const { const double L2 = SizeSquared(); if (L2 <= Tol) return { 0.0, 0.0 }; const double L = std::sqrt(L2); return { X / L, Y / L }; }
	static double Distance(const FVector2D& A, const FVector2D& B) { return (A - B).Size(); }
	static double DistSquared(const FVector2D& A, const FVector2D& B) { return (A - B).SizeSquared(); }
	static double DotProduct(const FVector2D& A, const FVector2D& B) { return A.X * B.X + A.Y * B.Y; }
	static const FVector2D ZeroVector;
};
inline const FVector2D FVector2D::ZeroVector(0.0, 0.0);

struct FVector {
	double X = 0.0, Y = 0.0, Z = 0.0;
	FVector() = default; FVector(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
	explicit FVector(const FVector2D& V, double InZ) : X(V.X), Y(V.Y), Z(InZ) {}
	FVector operator+(const FVector& O) const { return { X + O.X, Y + O.Y, Z + O.Z }; }
	FVector operator-(const FVector& O) const { return { X - O.X, Y - O.Y, Z - O.Z }; }
	FVector operator-() const { return { -X, -Y, -Z }; }
	FVector operator*(double K) const { return { X * K, Y * K, Z * K }; }
	double SizeSquared() const { return X * X + Y * Y + Z * Z; }
	FVector GetSafeNormal(double Tol = 1.e-8) const { const double L2 = SizeSquared(); if (L2 <= Tol) return { 0.0, 0.0, 0.0 }; const double L = std::sqrt(L2); return { X / L, Y / L, Z / L }; }
	bool Normalize(double Tol = 1.e-8) { const double L2 = SizeSquared(); if (L2 <= Tol) return false; const double L = std::sqrt(L2); X /= L; Y /= L; Z /= L; return true; }
	static FVector CrossProduct(const FVector& A, const FVector& B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
	static double DotProduct(const FVector& A, const FVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	static double Distance(const FVector& A, const FVector& B) { return std::sqrt((A - B).SizeSquared()); }
	FString ToCompactString() const { return FString::Printf("V(%.1f, %.1f, %.1f)", X, Y, Z); }
	static const FVector ZeroVector; static const FVector UpVector;
};
inline const FVector FVector::ZeroVector(0.0, 0.0, 0.0);
inline const FVector FVector::UpVector(0.0, 0.0, 1.0);
using FVector3d = FVector;
struct FVector3f { float X = 0, Y = 0, Z = 0; FVector3f() = default; explicit FVector3f(const FVector& V) : X((float)V.X), Y((float)V.Y), Z((float)V.Z) {} };
struct FVector2f { float X = 0, Y = 0; FVector2f() = default; FVector2f(float InX, float InY) : X(InX), Y(InY) {} };

inline float FMath::PerlinNoise2D(const FVector2D& P) {
	auto Hash = [](int32 X, int32 Y) { uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u; H = (H ^ (H >> 13)) * 1274126177u; return H ^ (H >> 16); };
	auto Grad = [&](int32 IX, int32 IY, double DX, double DY) { const double A = (Hash(IX, IY) % 3600) * (UE_DOUBLE_PI / 1800.0); return std::cos(A) * DX + std::sin(A) * DY; };
	const int32 X0 = static_cast<int32>(std::floor(P.X)), Y0 = static_cast<int32>(std::floor(P.Y));
	const double FX = P.X - X0, FY = P.Y - Y0;
	const double U = FX * FX * FX * (FX * (FX * 6 - 15) + 10), V = FY * FY * FY * (FY * (FY * 6 - 15) + 10);
	const double N00 = Grad(X0, Y0, FX, FY), N10 = Grad(X0 + 1, Y0, FX - 1, FY), N01 = Grad(X0, Y0 + 1, FX, FY - 1), N11 = Grad(X0 + 1, Y0 + 1, FX - 1, FY - 1);
	return static_cast<float>(1.4 * ((N00 + (N10 - N00) * U) + ((N01 + (N11 - N01) * U) - (N00 + (N10 - N00) * U)) * V));
}

struct FIntPoint { int32 X = 0, Y = 0; FIntPoint() = default; FIntPoint(int32 InX, int32 InY) : X(InX), Y(InY) {}
	bool operator==(const FIntPoint& O) const { return X == O.X && Y == O.Y; }
	bool operator<(const FIntPoint& O) const { return X != O.X ? X < O.X : Y < O.Y; } };

struct FBox2D {
	FVector2D Min, Max; bool bIsValid = false;
	FBox2D() = default; explicit FBox2D(EForceInit) {} FBox2D(const FVector2D& InMin, const FVector2D& InMax) : Min(InMin), Max(InMax), bIsValid(true) {}
	FBox2D& operator+=(const FVector2D& P) { if (bIsValid) { Min.X = std::min(Min.X, P.X); Min.Y = std::min(Min.Y, P.Y); Max.X = std::max(Max.X, P.X); Max.Y = std::max(Max.Y, P.Y); } else { Min = Max = P; bIsValid = true; } return *this; }
	FBox2D& operator+=(const FBox2D& O) { if (O.bIsValid) { *this += O.Min; *this += O.Max; } return *this; }
	FBox2D ExpandBy(double W) const { return FBox2D(FVector2D(Min.X - W, Min.Y - W), FVector2D(Max.X + W, Max.Y + W)); }
	bool Intersect(const FBox2D& O) const { return !(Min.X > O.Max.X || O.Min.X > Max.X || Min.Y > O.Max.Y || O.Min.Y > Max.Y); }
	bool IsInside(const FVector2D& P) const { return P.X > Min.X && P.X < Max.X && P.Y > Min.Y && P.Y < Max.Y; }
};

template <int N> struct TInlineAllocator {};
struct FDefaultAllocator {};

template <typename T, typename Alloc = FDefaultAllocator>
class TArray {
public:
	std::vector<T> V;
	TArray() = default;
	TArray(std::initializer_list<T> L) : V(L) {}
	template <typename OtherAlloc> TArray(const TArray<T, OtherAlloc>& O) : V(O.V) {}
	int32 Num() const { return static_cast<int32>(V.size()); }
	int32 Add(const T& X) { V.push_back(X); return Num() - 1; }
	int32 Add(T&& X) { V.push_back(std::move(X)); return Num() - 1; }
	T& AddDefaulted_GetRef() { V.emplace_back(); return V.back(); }
	int32 AddUnique(const T& X) { for (int32 i = 0; i < Num(); ++i) if (V[i] == X) return i; return Add(X); }
	void Reset(int32 = 0) { V.clear(); }
	void Empty(int32 = 0) { V.clear(); }
	void Reserve(int32 N) { V.reserve(N); }
	void Init(const T& X, int32 N) { V.assign(N, X); }
	void SetNum(int32 N) { V.resize(N); }
	void SetNumUninitialized(int32 N) { V.resize(N); }
	decltype(auto) operator[](int32 i) { check(i >= 0 && i < Num()); return V[i]; }
	decltype(auto) operator[](int32 i) const { check(i >= 0 && i < Num()); return V[i]; }
	T& Last() { return V.back(); } const T& Last() const { return V.back(); }
	bool IsValidIndex(int32 i) const { return i >= 0 && i < Num(); }
	int32 Remove(const T& X) { const auto Before = V.size(); V.erase(std::remove(V.begin(), V.end(), X), V.end()); return static_cast<int32>(Before - V.size()); }
	void RemoveAt(int32 i) { V.erase(V.begin() + i); }
	void RemoveAtSwap(int32 i) { V[i] = std::move(V.back()); V.pop_back(); }
	template <typename OtherAlloc> void Append(const TArray<T, OtherAlloc>& O) { V.insert(V.end(), O.V.begin(), O.V.end()); }
	void Sort() { std::sort(V.begin(), V.end()); }
	template <typename P> void Sort(P Pred) { std::sort(V.begin(), V.end(), Pred); }
	bool Contains(const T& X) const { return std::find(V.begin(), V.end(), X) != V.end(); }
	template <typename P> bool ContainsByPredicate(P Pred) const { return std::find_if(V.begin(), V.end(), Pred) != V.end(); }
	template <typename P> const T* FindByPredicate(P Pred) const { auto It = std::find_if(V.begin(), V.end(), Pred); return It == V.end() ? nullptr : &*It; }
	bool operator==(const TArray& O) const { return V == O.V; }
	const T* GetData() const { return V.data(); }
	auto begin() { return V.begin(); } auto end() { return V.end(); } auto begin() const { return V.begin(); } auto end() const { return V.end(); }
};

template <typename T>
class TConstArrayView {
public:
	const T* Data = nullptr; int32 Count = 0;
	TConstArrayView() = default;
	template <typename Alloc> TConstArrayView(const TArray<T, Alloc>& A) : Data(A.GetData()), Count(A.Num()) {}
	int32 Num() const { return Count; }
	const T& operator[](int32 i) const { check(i >= 0 && i < Count); return Data[i]; }
	const T* begin() const { return Data; } const T* end() const { return Data + Count; }
};

template <typename K, typename V> struct TPair { K Key; V Value; };

template <typename K, typename V>
class TMap {
public:
	std::map<K, TPair<K, V>> M;
	template <typename It, typename Ref> struct TIter { It I; Ref operator*() const { return I->second; } TIter& operator++() { ++I; return *this; } bool operator!=(const TIter& O) const { return I != O.I; } };
	using Iter = TIter<typename std::map<K, TPair<K, V>>::iterator, TPair<K, V>&>;
	using CIter = TIter<typename std::map<K, TPair<K, V>>::const_iterator, const TPair<K, V>&>;
	Iter begin() { return { M.begin() }; } Iter end() { return { M.end() }; } CIter begin() const { return { M.begin() }; } CIter end() const { return { M.end() }; }
	int32 Num() const { return static_cast<int32>(M.size()); }
	V& Add(const K& Key, const V& Value) { auto& P = M[Key]; P.Key = Key; P.Value = Value; return P.Value; }
	V& Add(const K& Key, V&& Value) { auto& P = M[Key]; P.Key = Key; P.Value = std::move(Value); return P.Value; }
	V& Add(const K& Key) { auto& P = M[Key]; P.Key = Key; P.Value = V(); return P.Value; }
	V* Find(const K& Key) { auto It = M.find(Key); return It == M.end() ? nullptr : &It->second.Value; }
	const V* Find(const K& Key) const { auto It = M.find(Key); return It == M.end() ? nullptr : &It->second.Value; }
	V& FindChecked(const K& Key) { auto It = M.find(Key); check(It != M.end()); return It->second.Value; }
	const V& FindChecked(const K& Key) const { auto It = M.find(Key); check(It != M.end()); return It->second.Value; }
	V FindRef(const K& Key) const { auto It = M.find(Key); return It == M.end() ? V() : It->second.Value; }
	V& FindOrAdd(const K& Key) { auto It = M.find(Key); if (It != M.end()) return It->second.Value; return Add(Key); }
	bool Contains(const K& Key) const { return M.count(Key) > 0; }
	int32 Remove(const K& Key) { return static_cast<int32>(M.erase(Key)); }
	bool RemoveAndCopyValue(const K& Key, V& Out) { auto It = M.find(Key); if (It == M.end()) return false; Out = std::move(It->second.Value); M.erase(It); return true; }
	void Reset() { M.clear(); }
	template <typename Alloc> int32 GetKeys(TArray<K, Alloc>& Out) const { Out.Reset(); for (const auto& P : M) Out.Add(P.first); return Out.Num(); }
};

template <typename T>
class TSet {
public:
	std::set<T> S;
	int32 Num() const { return static_cast<int32>(S.size()); }
	void Add(const T& X) { S.insert(X); }
	int32 Remove(const T& X) { return static_cast<int32>(S.erase(X)); }
	bool Contains(const T& X) const { return S.count(X) > 0; }
	void Reset() { S.clear(); }
	TArray<T> Array() const { TArray<T> A; for (const T& X : S) A.Add(X); return A; }
	template <typename Alloc> void Append(const TArray<T, Alloc>& A) { for (const T& X : A) S.insert(X); }
	auto begin() const { return S.begin(); } auto end() const { return S.end(); }
};

struct FRandomStream {
	uint32 State;
	explicit FRandomStream(int32 Seed = 0) : State(static_cast<uint32>(Seed) * 2654435761u + 12345u) {}
	uint32 Next() { State = State * 1664525u + 1013904223u; return State >> 8; }
	float FRand() { return (Next() & 0xFFFFFF) / 16777216.0f; }
	float FRandRange(float A, float B) { return A + (B - A) * FRand(); }
	int32 RandRange(int32 A, int32 B) { return A + static_cast<int32>(Next() % static_cast<uint32>(B - A + 1)); }
};

class FArchive {
public:
	virtual ~FArchive() = default;
	bool bLoading = false; bool bError = false;
	bool IsLoading() const { return bLoading; } bool IsSaving() const { return !bLoading; } bool IsError() const { return bError; } void SetError() { bError = true; }
	virtual void Serialize(void* Data, int64 Num) = 0;
};
template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0> FArchive& operator<<(FArchive& Ar, T& X) { Ar.Serialize(&X, sizeof(T)); return Ar; }
inline FArchive& operator<<(FArchive& Ar, FVector2D& V) { return Ar << V.X << V.Y; }
inline FArchive& operator<<(FArchive& Ar, FVector& V) { return Ar << V.X << V.Y << V.Z; }
inline FArchive& operator<<(FArchive& Ar, FString& S) { int32 N = static_cast<int32>(S.S.size()); Ar << N; if (Ar.IsLoading()) { if (N < 0 || N > (1 << 20)) { Ar.SetError(); return Ar; } S.S.resize(N); } if (N > 0) Ar.Serialize(S.S.data(), N); return Ar; }
template <typename T, typename A> FArchive& operator<<(FArchive& Ar, TArray<T, A>& Arr) { int32 N = Arr.Num(); Ar << N; if (Ar.IsLoading()) { if (N < 0 || N > (1 << 28) || Ar.IsError()) { Ar.SetError(); return Ar; } Arr.SetNum(N); } for (int32 i = 0; i < N && !Ar.IsError(); ++i) Ar << Arr.V[i]; return Ar; }

template <typename T> struct TObjectPtr { T* P = nullptr; operator T*() const { return P; } T* operator->() const { return P; } };
template <typename T> struct TSoftObjectPtr { T* LoadSynchronous() const { return nullptr; } };
template <typename T> const T* GetDefault() { static T Instance; return &Instance; }
class UObject {}; class UMaterialInterface; class UStaticMesh;
