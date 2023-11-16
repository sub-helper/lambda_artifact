/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

#include "BundleResources.h"
#include "Debug.h"
#include "RedexMappedFile.h"
#include "RedexResources.h"
#include "RedexTestUtils.h"
#include "Trace.h"

using namespace boost::filesystem;

namespace {

void copy_file(const std::string& from, const std::string& to) {
  std::ifstream src_stream(from, std::ios::binary);
  std::ofstream dest_stream(to, std::ios::binary);
  dest_stream << src_stream.rdbuf();
}

void setup_resources_and_run(
    const std::function<void(const std::string& extract_dir, BundleResources*)>&
        callback) {
  auto tmp_dir = redex::make_tmp_dir("BundleResourcesTest%%%%%%%%");
  boost::filesystem::path p(tmp_dir.path);

  auto res_dir = p / "base";
  create_directories(res_dir);
  copy_file(std::getenv("test_res_path"), res_dir.string() + "/resources.pb");

  auto manifest_dir = p / "base/manifest";
  create_directories(manifest_dir);
  copy_file(std::getenv("test_manifest_path"),
            manifest_dir.string() + "/AndroidManifest.xml");

  auto layout_dir = p / "base/res/layout";
  create_directories(layout_dir);
  auto layout_dest = layout_dir.string() + "/activity_main.xml";
  copy_file(std::getenv("test_layout_path"), layout_dest);

  BundleResources resources(tmp_dir.path);
  callback(tmp_dir.path, &resources);
}

ComponentTagInfo find_component_info(const std::vector<ComponentTagInfo>& list,
                                     const std::string& classname) {
  for (const auto& info : list) {
    if (classname == info.classname) {
      return info;
    }
  }
  throw std::runtime_error("Not found: " + classname);
}
} // namespace

TEST(BundleResources, TestReadMinSdk) {
  setup_resources_and_run(
      [&](const std::string& extract_dir, BundleResources* resources) {
        auto result = resources->get_min_sdk();
        EXPECT_EQ(*result, 21);
      });
}

TEST(BundleResources, TestReadManifestClasses) {
  setup_resources_and_run([&](const std::string& extract_dir,
                              BundleResources* resources) {
    auto manifest_info = resources->get_manifest_class_info();
    auto app_classes = manifest_info.application_classes;
    EXPECT_EQ(app_classes.count("Lcom/fb/bundles/MyApplication;"), 1);
    EXPECT_EQ(app_classes.count("Lcom/fb/bundles/MyAppComponentFactory;"), 1);

    EXPECT_EQ(manifest_info.instrumentation_classes.count(
                  "Landroidx/test/runner/AndroidJUnitRunner;"),
              1);

    auto provider = find_component_info(manifest_info.component_tags,
                                        "Lcom/fb/bundles/MyContentProvider;");
    EXPECT_FALSE(provider.has_intent_filters);
    EXPECT_EQ(provider.is_exported, BooleanXMLAttribute::True);
    EXPECT_EQ(provider.permission, "com.fb.bundles.REALLY_SERIOUS");
    EXPECT_EQ(provider.authority_classes.size(), 2);
    EXPECT_EQ(provider.authority_classes.count("Lyo;"), 1);
    EXPECT_EQ(provider.authority_classes.count("Lsup;"), 1);

    auto receiver = find_component_info(manifest_info.component_tags,
                                        "Lcom/fb/bundles/MyReceiver;");
    EXPECT_TRUE(receiver.has_intent_filters);
    EXPECT_EQ(receiver.is_exported, BooleanXMLAttribute::True);
    EXPECT_EQ(receiver.permission, "com.fb.bundles.REALLY_SERIOUS");
    EXPECT_EQ(receiver.authority_classes.size(), 0);

    auto service = find_component_info(manifest_info.component_tags,
                                       "Lcom/fb/bundles/MyIntentService;");
    EXPECT_FALSE(service.has_intent_filters);
    EXPECT_EQ(service.is_exported, BooleanXMLAttribute::False);
    EXPECT_EQ(service.authority_classes.size(), 0);

    auto public_activity = find_component_info(
        manifest_info.component_tags, "Lcom/fb/bundles/PublicActivity;");
    EXPECT_FALSE(public_activity.has_intent_filters);
    EXPECT_EQ(public_activity.is_exported, BooleanXMLAttribute::True);
    EXPECT_EQ(public_activity.authority_classes.size(), 0);

    auto private_activity = find_component_info(
        manifest_info.component_tags, "Lcom/fb/bundles/PrivateActivity;");
    EXPECT_FALSE(private_activity.has_intent_filters);
    EXPECT_EQ(private_activity.is_exported, BooleanXMLAttribute::False);
    EXPECT_EQ(private_activity.authority_classes.size(), 0);

    auto main_activity = find_component_info(manifest_info.component_tags,
                                             "Lcom/fb/bundles/MainActivity;");
    EXPECT_TRUE(main_activity.has_intent_filters);
    EXPECT_EQ(main_activity.is_exported, BooleanXMLAttribute::Undefined);
    EXPECT_EQ(main_activity.authority_classes.size(), 0);

    bool found_alias = false;
    for (const auto& info : manifest_info.component_tags) {
      if (info.tag == ComponentTag::ActivityAlias) {
        found_alias = true;
        EXPECT_EQ(info.classname, "Lcom/fb/bundles/PublicActivity;");
      }
    }
    EXPECT_TRUE(found_alias);
  });
}

// Test collecting resource ids from xml attributes.
TEST(BundleResources, TestCollectRidsFromXmlAttrs) {
  setup_resources_and_run(
      [&](const std::string& path, BundleResources* resources) {
        auto rids = resources->get_xml_reference_attributes(
            path + "/base/manifest/AndroidManifest.xml");
        // @string/app_name, @drawable/icon and @style/ThemeA
        EXPECT_EQ(rids.size(), 3);
      });
}

// Test collecting resource ids from xml attributes.
TEST(BundleResources, TestCollectResFilesByRid) {
  setup_resources_and_run([&](const std::string& /* extract_dir */,
                              BundleResources* resources) {
    auto res_table = resources->load_res_table();

    auto icon_ids = res_table->get_res_ids_by_name("icon");
    EXPECT_EQ(icon_ids.size(), 1);
    auto files = res_table->get_files_by_rid(icon_ids[0]);
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(*files.begin(), "res/drawable-mdpi-v4/icon.png");
    files = res_table->get_files_by_rid(icon_ids[0], ResourcePathType::ZipPath);
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(*files.begin(), "base/res/drawable-mdpi-v4/icon.png")
        << "file path incorrect or base module not appended";

    auto prickly_ids = res_table->get_res_ids_by_name("prickly");
    EXPECT_EQ(prickly_ids.size(), 1);
    files = res_table->get_files_by_rid(prickly_ids[0]);
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(*files.begin(), "res/drawable-mdpi-v4/prickly.png");

    auto padding_right_ids = res_table->get_res_ids_by_name("padding_right");
    EXPECT_EQ(padding_right_ids.size(), 1);
    files = res_table->get_files_by_rid(prickly_ids[0]);
    EXPECT_EQ(files.size(), 1);
  });
}

TEST(BundleResources, ReadLayout) {
  setup_resources_and_run(
      [&](const std::string& extract_dir, BundleResources* resources) {
        std::unordered_set<std::string> layout_classes;
        std::unordered_set<std::string> attrs_to_read;
        attrs_to_read.emplace(ONCLICK_ATTRIBUTE);
        std::unordered_multimap<std::string, std::string> attribute_values;
        resources->collect_layout_classes_and_attributes_for_file(
            std::getenv("test_layout_path"),
            attrs_to_read,
            &layout_classes,
            &attribute_values);
        EXPECT_EQ(layout_classes.size(), 2);
        EXPECT_EQ(layout_classes.count("Lcom/fb/bundles/WickedCoolButton;"), 1);
        EXPECT_EQ(layout_classes.count("Lcom/fb/bundles/NiftyViewGroup;"), 1);

        auto range = attribute_values.equal_range(ONCLICK_ATTRIBUTE);
        size_t found_method_names = 0;
        for (auto it = range.first; it != range.second; ++it) {
          found_method_names++;
          EXPECT_TRUE(it->second == "performFoo" || it->second == "performBar");
        }
        EXPECT_EQ(found_method_names, 2);
      });
}

TEST(BundleResources, RenameLayout) {
  setup_resources_and_run(
      [&](const std::string& extract_dir, BundleResources* resources) {
        std::map<std::string, std::string> rename_map;
        rename_map.emplace("com.fb.bundles.WickedCoolButton", "X.001");
        rename_map.emplace("com.fb.bundles.NiftyViewGroup", "X.002");
        resources->rename_classes_in_layouts(rename_map);

        // Read the file again to see it take effect
        std::unordered_set<std::string> layout_classes;
        std::unordered_set<std::string> attrs_to_read;
        std::unordered_multimap<std::string, std::string> attribute_values;
        resources->collect_layout_classes_and_attributes_for_file(
            extract_dir + "/base/res/layout/activity_main.xml",
            attrs_to_read,
            &layout_classes,
            &attribute_values);
        EXPECT_EQ(layout_classes.size(), 2);
        EXPECT_EQ(layout_classes.count("LX/001;"), 1);
        EXPECT_EQ(layout_classes.count("LX/002;"), 1);
      });
}

TEST(BundleResources, ReadResource) {
  setup_resources_and_run([&](const std::string& /* extract_dir */,
                              BundleResources* resources) {
    auto res_table = resources->load_res_table();
    auto padding_left_ids = res_table->get_res_ids_by_name("padding_left");
    EXPECT_EQ(padding_left_ids.size(), 1);
    auto module_name =
        reinterpret_cast<ResourcesPbFile*>(res_table.get())
            ->resolve_module_name_for_resource_id(padding_left_ids[0]);
    EXPECT_STREQ("base", module_name.c_str());
    const auto& id_to_name = res_table->id_to_name;
    auto obtain_resource_name_back = id_to_name.at(padding_left_ids[0]);
    EXPECT_EQ(obtain_resource_name_back, "padding_left");
    auto bg_grey = res_table->get_res_ids_by_name("bg_grey");
    EXPECT_EQ(bg_grey.size(), 1);
    obtain_resource_name_back = id_to_name.at(bg_grey[0]);
    EXPECT_EQ(obtain_resource_name_back, "bg_grey");
    auto drawable_type_id = res_table->get_types_by_name({"drawable"});
    EXPECT_EQ(drawable_type_id.size(), 1);
    std::unordered_set<std::string> drawable_res_names;
    for (const auto& pair : id_to_name) {
      auto id = pair.first;
      if (drawable_type_id.count(id & TYPE_MASK_BIT)) {
        drawable_res_names.emplace(pair.second);
      }
    }
    EXPECT_EQ(drawable_res_names.size(), 2);
    EXPECT_EQ(drawable_res_names.count("icon"), 1);
    EXPECT_EQ(drawable_res_names.count("prickly"), 1);

    auto padding_right_ids = res_table->get_res_ids_by_name("padding_right");
    EXPECT_EQ(padding_right_ids.size(), 1);
    EXPECT_EQ(res_table->resource_value_identical(padding_left_ids[0],
                                                  padding_right_ids[0]),
              true);

    auto unused_dimen_2_ids = res_table->get_res_ids_by_name("unused_dimen_2");
    EXPECT_EQ(unused_dimen_2_ids.size(), 1);
    EXPECT_EQ(res_table->resource_value_identical(padding_left_ids[0],
                                                  unused_dimen_2_ids[0]),
              true);

    auto margin_top_ids = res_table->get_res_ids_by_name("margin_top");
    EXPECT_EQ(margin_top_ids.size(), 1);
    EXPECT_EQ(res_table->resource_value_identical(padding_left_ids[0],
                                                  margin_top_ids[0]),
              false);

    auto prickly_ids = res_table->get_res_ids_by_name("prickly");
    EXPECT_EQ(prickly_ids.size(), 1);
    EXPECT_EQ(res_table->resource_value_identical(padding_left_ids[0],
                                                  prickly_ids[0]),
              false);

    auto foo_ids = res_table->get_res_ids_by_name("foo");
    EXPECT_EQ(foo_ids.size(), 1);
    auto bar_ids = res_table->get_res_ids_by_name("bar");
    EXPECT_EQ(bar_ids.size(), 1);
    auto far_ids = res_table->get_res_ids_by_name("far");
    EXPECT_EQ(far_ids.size(), 1);
    auto baz_ids = res_table->get_res_ids_by_name("baz");
    EXPECT_EQ(baz_ids.size(), 1);
    auto boo_ids = res_table->get_res_ids_by_name("boo");
    EXPECT_EQ(boo_ids.size(), 1);

    EXPECT_EQ(res_table->resource_value_identical(foo_ids[0], bar_ids[0]),
              true);
    EXPECT_EQ(res_table->resource_value_identical(bar_ids[0], far_ids[0]),
              false);
    EXPECT_EQ(res_table->resource_value_identical(baz_ids[0], boo_ids[0]),
              false);

    const auto& res_table_pb = static_cast<ResourcesPbFile*>(res_table.get());
    const auto& id_to_configvalue = res_table_pb->get_res_id_to_configvalue();
    EXPECT_EQ(res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(padding_left_ids[0])),
              res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(padding_right_ids[0])));
    EXPECT_EQ(res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(padding_left_ids[0])),
              res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(unused_dimen_2_ids[0])));
    EXPECT_NE(res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(padding_left_ids[0])),
              res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(margin_top_ids[0])));
    EXPECT_NE(res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(padding_left_ids[0])),
              res_table_pb->get_hash_from_values(
                  id_to_configvalue.at(prickly_ids[0])));
    EXPECT_EQ(
        res_table_pb->get_hash_from_values(id_to_configvalue.at(foo_ids[0])),
        res_table_pb->get_hash_from_values(id_to_configvalue.at(bar_ids[0])));
    EXPECT_NE(
        res_table_pb->get_hash_from_values(id_to_configvalue.at(far_ids[0])),
        res_table_pb->get_hash_from_values(id_to_configvalue.at(bar_ids[0])));
    EXPECT_NE(
        res_table_pb->get_hash_from_values(id_to_configvalue.at(baz_ids[0])),
        res_table_pb->get_hash_from_values(id_to_configvalue.at(boo_ids[0])));
  });
}

TEST(BundleResources, WriteResource) {
  setup_resources_and_run([&](const std::string& extract_dir,
                              BundleResources* resources) {
    auto res_table = resources->load_res_table();
    auto padding_left_ids = res_table->get_res_ids_by_name("padding_left");
    EXPECT_EQ(padding_left_ids.size(), 1);
    auto padding_left_id = padding_left_ids[0];
    auto padding_right_ids = res_table->get_res_ids_by_name("padding_right");
    EXPECT_EQ(padding_right_ids.size(), 1);
    auto padding_right_id = padding_right_ids[0];
    auto unused_dimen_1_ids = res_table->get_res_ids_by_name("unused_dimen_1");
    EXPECT_EQ(unused_dimen_1_ids.size(), 1);
    auto unused_dimen_1_id = unused_dimen_1_ids[0];
    auto unused_dimen_2_ids = res_table->get_res_ids_by_name("unused_dimen_2");
    EXPECT_EQ(unused_dimen_2_ids.size(), 1);
    auto unused_dimen_2_id = unused_dimen_2_ids[0];

    res_table->delete_resource(unused_dimen_1_id);
    res_table->delete_resource(unused_dimen_2_id);
    std::map<uint32_t, uint32_t> to_replace;
    to_replace[padding_left_id] = unused_dimen_1_id;
    to_replace[padding_right_id] = unused_dimen_2_id;

    res_table->remap_res_ids_and_serialize({extract_dir + "/base/resources.pb"},
                                           to_replace);
    auto res_table_new = resources->load_res_table();

    EXPECT_EQ(res_table_new->get_res_ids_by_name("unused_dimen_2").size(), 0);
    EXPECT_EQ(res_table_new->get_res_ids_by_name("unused_dimen_1").size(), 0);
    padding_left_ids = res_table_new->get_res_ids_by_name("padding_left");
    EXPECT_EQ(padding_left_ids.size(), 1);
    EXPECT_EQ(padding_left_ids[0], unused_dimen_1_id);
    padding_right_ids = res_table_new->get_res_ids_by_name("padding_right");
    EXPECT_EQ(padding_right_ids.size(), 1);
    EXPECT_EQ(padding_right_ids[0], unused_dimen_2_id);
  });
}

TEST(BundleResources, ChangeResourceIdInLayout) {
  setup_resources_and_run([&](const std::string& extract_dir,
                              BundleResources* resources) {
    auto res_table = resources->load_res_table();
    auto margin_top_ids = res_table->get_res_ids_by_name("margin_top");
    EXPECT_EQ(margin_top_ids.size(), 1);
    auto margin_top_id = margin_top_ids[0];
    auto padding_right_ids = res_table->get_res_ids_by_name("padding_right");
    EXPECT_EQ(padding_right_ids.size(), 1);
    auto padding_right_id = padding_right_ids[0];
    auto prickly_ids = res_table->get_res_ids_by_name("prickly");
    EXPECT_EQ(prickly_ids.size(), 1);
    auto prickly_id = prickly_ids[0];
    auto icon_ids = res_table->get_res_ids_by_name("icon");
    EXPECT_EQ(icon_ids.size(), 1);
    auto icon_id = icon_ids[0];
    std::map<uint32_t, uint32_t> kept_to_remapped_ids;
    kept_to_remapped_ids[prickly_id] = icon_id;
    kept_to_remapped_ids[margin_top_id] = padding_right_id;
    auto changed = resources->remap_xml_reference_attributes(
        extract_dir + "/base/res/layout/activity_main.xml",
        kept_to_remapped_ids);
    EXPECT_EQ(changed, 4);
  });
}
