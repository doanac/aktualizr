#include <gtest/gtest.h>

#include "version.h"

TEST(version, bad_versions) {
  ASSERT_TRUE(Version("bar") < Version("foo"));
  ASSERT_TRUE(Version("1.bar") < Version("2foo"));
  ASSERT_TRUE(Version("1..0") < Version("1.1"));
  ASSERT_TRUE(Version("1.-1") < Version("1.1"));
  ASSERT_TRUE(Version("1.*bad #text") < Version("1.1"));  // ord('*') < ord('1')
}

TEST(version, good_versions) {
  ASSERT_TRUE(Version("1.0.1") < Version("1.0.1.1"));
  ASSERT_TRUE(Version("1.0.1") < Version("1.0.2"));
  ASSERT_TRUE(Version("0.9") < Version("1.0.1"));
  ASSERT_TRUE(Version("1.0.0.0") < Version("1.0.0.1"));
  ASSERT_TRUE(Version("1") < Version("1.0.0.1"));
  ASSERT_TRUE(Version("1.9.0") < Version("1.10"));
}

TEST(version, target_has_tags) {
  auto t = Uptane::Target::Unknown();

  // No tags defined in target:
  std::vector<std::string> config_tags;
  ASSERT_TRUE(target_has_tags(t, config_tags));
  config_tags.push_back("foo");
  ASSERT_FALSE(target_has_tags(t, config_tags));

  // Set target tags to: premerge, qa
  auto custom = t.custom_data();
  custom["tags"].append("premerge");
  custom["tags"].append("qa");
  t.updateCustom(custom);

  config_tags.clear();
  ASSERT_TRUE(target_has_tags(t, config_tags));

  config_tags.push_back("qa");
  config_tags.push_back("blah");
  ASSERT_TRUE(target_has_tags(t, config_tags));

  config_tags.clear();
  config_tags.push_back("premerge");
  ASSERT_TRUE(target_has_tags(t, config_tags));

  config_tags.clear();
  config_tags.push_back("foo");
  ASSERT_FALSE(target_has_tags(t, config_tags));
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
#endif
