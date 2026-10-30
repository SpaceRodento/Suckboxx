#pragma once
// Minimaalinen Unity-yhteensopiva testiotsikko native-testien ajamiseen ilman
// PlatformIO:ta (tools/run_native_tests.sh). PlatformIO:lla (`pio test -e native`)
// käytetään oikeaa Unitya - tämä tiedosto ei ole silloin include-polussa.
//
// Tuetut makrot on lueteltu docs/firmware.md:ssä. Älä käytä muita Unity-makroja
// testeissä, tai lisää ne tänne samalla semantiikalla kuin Unityssa.

#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace unity_shim {
inline int& failures() { static int v = 0; return v; }
inline int& tests() { static int v = 0; return v; }
inline std::jmp_buf& jump() { static std::jmp_buf b; return b; }
inline const char*& current() { static const char* n = ""; return n; }

[[noreturn]] inline void fail(const char* file, int line, const char* what, const char* msg) {
  std::printf("%s:%d:%s:FAIL: %s%s%s\n", file, line, current(), what,
              msg ? " - " : "", msg ? msg : "");
  failures()++;
  std::longjmp(jump(), 1);
}

inline void check(bool ok, const char* file, int line, const char* what, const char* msg) {
  if (!ok) fail(file, line, what, msg);
}

inline void checkInt(long long expected, long long actual, const char* file, int line, const char* msg) {
  if (expected != actual) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Expected %lld Was %lld", expected, actual);
    fail(file, line, buf, msg);
  }
}

inline void checkIntWithin(long long delta, long long expected, long long actual, const char* file, int line, const char* msg) {
  long long d = expected > actual ? expected - actual : actual - expected;
  if (d > delta) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Expected %lld +/- %lld Was %lld", expected, delta, actual);
    fail(file, line, buf, msg);
  }
}

inline void checkFloatWithin(float delta, float expected, float actual, const char* file, int line, const char* msg) {
  if (std::isnan(actual) || std::isnan(expected) || std::fabs(expected - actual) > delta) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Expected %.6g +/- %.6g Was %.6g",
                  static_cast<double>(expected), static_cast<double>(delta), static_cast<double>(actual));
    fail(file, line, buf, msg);
  }
}

inline void checkCmp(bool ok, long long threshold, long long actual, const char* rel, const char* file, int line, const char* msg) {
  if (!ok) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Expected %s %lld Was %lld", rel, threshold, actual);
    fail(file, line, buf, msg);
  }
}

inline void checkFloatCmp(bool ok, float threshold, float actual, const char* rel, const char* file, int line, const char* msg) {
  if (!ok) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Expected %s %.6g Was %.6g", rel,
                  static_cast<double>(threshold), static_cast<double>(actual));
    fail(file, line, buf, msg);
  }
}

inline void checkStr(const char* expected, const char* actual, const char* file, int line, const char* msg) {
  if (expected == nullptr || actual == nullptr || std::strcmp(expected, actual) != 0) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "Expected \"%s\" Was \"%s\"", expected ? expected : "(null)",
                  actual ? actual : "(null)");
    fail(file, line, buf, msg);
  }
}
}  // namespace unity_shim

void setUp();
void tearDown();

#define UNITY_BEGIN() (unity_shim::failures() = 0, unity_shim::tests() = 0, 0)
#define UNITY_END()                                                                         \
  (std::printf("\n-----------------------\n%d Tests %d Failures 0 Ignored\n%s\n",          \
               unity_shim::tests(), unity_shim::failures(),                                 \
               unity_shim::failures() ? "FAIL" : "OK"),                                     \
   unity_shim::failures())
#define RUN_TEST(fn)                                                \
  do {                                                              \
    unity_shim::current() = #fn;                                    \
    unity_shim::tests()++;                                          \
    if (setjmp(unity_shim::jump()) == 0) {                          \
      setUp();                                                      \
      fn();                                                         \
      std::printf("%s:%d:%s:PASS\n", __FILE__, __LINE__, #fn);      \
    }                                                               \
    tearDown();                                                     \
  } while (0)

#define TEST_FAIL_MESSAGE(msg) unity_shim::fail(__FILE__, __LINE__, "FAIL", msg)
#define TEST_FAIL() TEST_FAIL_MESSAGE(nullptr)
#define TEST_MESSAGE(msg) std::printf("%s:%d:%s:INFO: %s\n", __FILE__, __LINE__, unity_shim::current(), msg)

#define TEST_ASSERT_MESSAGE(c, msg) unity_shim::check(static_cast<bool>(c), __FILE__, __LINE__, "Expression Evaluated To FALSE", msg)
#define TEST_ASSERT(c) TEST_ASSERT_MESSAGE(c, nullptr)
#define TEST_ASSERT_TRUE_MESSAGE(c, msg) TEST_ASSERT_MESSAGE(c, msg)
#define TEST_ASSERT_TRUE(c) TEST_ASSERT_MESSAGE(c, nullptr)
#define TEST_ASSERT_FALSE_MESSAGE(c, msg) unity_shim::check(!(c), __FILE__, __LINE__, "Expected FALSE Was TRUE", msg)
#define TEST_ASSERT_FALSE(c) TEST_ASSERT_FALSE_MESSAGE(c, nullptr)
#define TEST_ASSERT_NULL(p) unity_shim::check((p) == nullptr, __FILE__, __LINE__, "Expected NULL", nullptr)
#define TEST_ASSERT_NOT_NULL(p) unity_shim::check((p) != nullptr, __FILE__, __LINE__, "Expected Non-NULL", nullptr)

#define TEST_ASSERT_EQUAL_INT_MESSAGE(e, a, msg) unity_shim::checkInt(static_cast<long long>(e), static_cast<long long>(a), __FILE__, __LINE__, msg)
#define TEST_ASSERT_EQUAL_INT(e, a) TEST_ASSERT_EQUAL_INT_MESSAGE(e, a, nullptr)
#define TEST_ASSERT_EQUAL(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_MESSAGE(e, a, msg) TEST_ASSERT_EQUAL_INT_MESSAGE(e, a, msg)
#define TEST_ASSERT_EQUAL_INT8(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_INT16(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_INT32(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_UINT(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_UINT8(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_UINT16(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_UINT32(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_size_t(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_INT_WITHIN_MESSAGE(d, e, a, msg) unity_shim::checkIntWithin(static_cast<long long>(d), static_cast<long long>(e), static_cast<long long>(a), __FILE__, __LINE__, msg)
#define TEST_ASSERT_INT_WITHIN(d, e, a) TEST_ASSERT_INT_WITHIN_MESSAGE(d, e, a, nullptr)
#define TEST_ASSERT_UINT32_WITHIN(d, e, a) TEST_ASSERT_INT_WITHIN(d, e, a)

// Unity-semantiikka: GREATER_THAN(threshold, actual) tarkoittaa actual > threshold.
#define TEST_ASSERT_GREATER_THAN_MESSAGE(t, a, msg) unity_shim::checkCmp((a) > (t), static_cast<long long>(t), static_cast<long long>(a), ">", __FILE__, __LINE__, msg)
#define TEST_ASSERT_GREATER_THAN(t, a) TEST_ASSERT_GREATER_THAN_MESSAGE(t, a, nullptr)
#define TEST_ASSERT_LESS_THAN_MESSAGE(t, a, msg) unity_shim::checkCmp((a) < (t), static_cast<long long>(t), static_cast<long long>(a), "<", __FILE__, __LINE__, msg)
#define TEST_ASSERT_LESS_THAN(t, a) TEST_ASSERT_LESS_THAN_MESSAGE(t, a, nullptr)
#define TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(t, a, msg) unity_shim::checkCmp((a) >= (t), static_cast<long long>(t), static_cast<long long>(a), ">=", __FILE__, __LINE__, msg)
#define TEST_ASSERT_GREATER_OR_EQUAL(t, a) TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(t, a, nullptr)
#define TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(t, a, msg) unity_shim::checkCmp((a) <= (t), static_cast<long long>(t), static_cast<long long>(a), "<=", __FILE__, __LINE__, msg)
#define TEST_ASSERT_LESS_OR_EQUAL(t, a) TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(t, a, nullptr)

#define TEST_ASSERT_FLOAT_WITHIN_MESSAGE(d, e, a, msg) unity_shim::checkFloatWithin(static_cast<float>(d), static_cast<float>(e), static_cast<float>(a), __FILE__, __LINE__, msg)
#define TEST_ASSERT_FLOAT_WITHIN(d, e, a) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(d, e, a, nullptr)
#define TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e, a, msg) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f * (std::fabs(static_cast<float>(e)) > 1.0f ? std::fabs(static_cast<float>(e)) : 1.0f), e, a, msg)
#define TEST_ASSERT_EQUAL_FLOAT(e, a) TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e, a, nullptr)
#define TEST_ASSERT_GREATER_THAN_FLOAT(t, a) unity_shim::checkFloatCmp((a) > (t), static_cast<float>(t), static_cast<float>(a), ">", __FILE__, __LINE__, nullptr)
#define TEST_ASSERT_LESS_THAN_FLOAT(t, a) unity_shim::checkFloatCmp((a) < (t), static_cast<float>(t), static_cast<float>(a), "<", __FILE__, __LINE__, nullptr)

#define TEST_ASSERT_EQUAL_STRING_MESSAGE(e, a, msg) unity_shim::checkStr(e, a, __FILE__, __LINE__, msg)
#define TEST_ASSERT_EQUAL_STRING(e, a) TEST_ASSERT_EQUAL_STRING_MESSAGE(e, a, nullptr)
