#ifndef _TEST_REGTEST_ASSERTS_H_
#define _TEST_REGTEST_ASSERTS_H_

#include <gtest/gtest.h>

#define EXPECT_THAT_AND_RETURN(a, match) ([&](){ auto&& _actual = (a); EXPECT_THAT(_actual, match); return _actual; }())

#endif // !_TEST_REGTEST_ASSERTS_H_
