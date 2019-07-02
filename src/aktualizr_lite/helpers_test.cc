#include <gtest/gtest.h>

#include "helpers.h"

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

TEST(version, targets_eq) {
  auto t1 = Uptane::Target::Unknown();
  auto t2 = Uptane::Target::Unknown();

  // t1 should equal t2 when there a no docker-apps
  ASSERT_TRUE(targets_eq(t1, t2, false));
  ASSERT_TRUE(targets_eq(t1, t2, true));

  auto custom = t1.custom_data();
  custom["docker_apps"]["app1"]["filename"] = "app1-v1";
  t1.updateCustom(custom);
  ASSERT_TRUE(targets_eq(t1, t2, false));  // still equal, ignoring docker-apps
  ASSERT_FALSE(targets_eq(t1, t2, true));

  custom = t2.custom_data();
  custom["docker_apps"]["app1"]["filename"] = "app1-v1";
  t2.updateCustom(custom);
  ASSERT_TRUE(targets_eq(t1, t2, true));

  custom["docker_apps"]["app1"]["filename"] = "app1-v2";
  t2.updateCustom(custom);
  ASSERT_FALSE(targets_eq(t1, t2, true));  // version has changed

  // Get things the same again
  custom["docker_apps"]["app1"]["filename"] = "app1-v1";
  t2.updateCustom(custom);

  custom["docker_apps"]["app2"]["filename"] = "app2-v2";
  t2.updateCustom(custom);
  ASSERT_FALSE(targets_eq(t1, t2, true));  // t2 has an app that t1 doesn't

  custom = t1.custom_data();
  custom["docker_apps"]["app2"]["filename"] = "app2-v1";
  t1.updateCustom(custom);
  ASSERT_FALSE(targets_eq(t1, t2, true));  // app2 versions differ

  custom["docker_apps"]["app2"]["filename"] = "app2-v2";
  t1.updateCustom(custom);
  ASSERT_TRUE(targets_eq(t1, t2, true));
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
#endif
