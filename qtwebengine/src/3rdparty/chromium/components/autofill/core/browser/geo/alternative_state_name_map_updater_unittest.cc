// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/geo/alternative_state_name_map_updater.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/optional.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/geo/alternative_state_name_map.h"
#include "components/autofill/core/browser/geo/alternative_state_name_map_test_utils.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_personal_data_manager.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::ASCIIToUTF16;
using base::UTF8ToUTF16;

namespace autofill {

class MockAlternativeStateNameMapUpdater
    : public AlternativeStateNameMapUpdater {
 public:
  MockAlternativeStateNameMapUpdater(base::OnceClosure callback,
                                     PrefService* local_state,
                                     PersonalDataManager* personal_data_manager)
      : AlternativeStateNameMapUpdater(local_state, personal_data_manager),
        callback_(std::move(callback)) {}

  // PersonalDataManagerObserver:
  void OnPersonalDataFinishedProfileTasks() override {
    if (base::FeatureList::IsEnabled(
            features::kAutofillUseAlternativeStateNameMap)) {
      PopulateAlternativeStateNameMap(std::move(callback_));
    }
  }

 private:
  base::OnceClosure callback_;
};

class AlternativeStateNameMapUpdaterTest : public ::testing::Test {
 public:
  AlternativeStateNameMapUpdaterTest() = default;

  void SetUp() override {
    feature_.InitAndEnableFeature(
        features::kAutofillUseAlternativeStateNameMap);

    autofill_client_.SetPrefs(test::PrefServiceForTesting());
    ASSERT_TRUE(data_install_dir_.CreateUniqueTempDir());
    personal_data_manager_.Init(/*profile_database=*/database_,
                                /*account_database=*/nullptr,
                                /*pref_service=*/autofill_client_.GetPrefs(),
                                /*local_state=*/autofill_client_.GetPrefs(),
                                /*identity_manager=*/nullptr,
                                /*client_profile_validator=*/nullptr,
                                /*history_service=*/nullptr,
                                /*is_off_the_record=*/false);
    alternative_state_name_map_updater_ =
        std::make_unique<AlternativeStateNameMapUpdater>(
            autofill_client_.GetPrefs(), &personal_data_manager_);
  }

  const base::FilePath& GetPath() const { return data_install_dir_.GetPath(); }

  void WritePathToPref(const base::FilePath& file_path) {
    autofill_client_.GetPrefs()->SetFilePath(
        autofill::prefs::kAutofillStatesDataDir, file_path);
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  TestAutofillClient autofill_client_;
  scoped_refptr<AutofillWebDataService> database_;
  std::unique_ptr<AlternativeStateNameMapUpdater>
      alternative_state_name_map_updater_;
  base::ScopedTempDir data_install_dir_;
  base::test::ScopedFeatureList feature_;
  TestPersonalDataManager personal_data_manager_;
};

// Tests that the states data is added to AlternativeStateNameMap.
TEST_F(AlternativeStateNameMapUpdaterTest, EntryAddedToStateMap) {
  test::ClearAlternativeStateNameMapForTesting();
  std::string states_data = test::CreateStatesProtoAsString();
  std::vector<AlternativeStateNameMap::StateName> test_strings = {
      AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria")),
      AlternativeStateNameMap::StateName(ASCIIToUTF16("Bayern")),
      AlternativeStateNameMap::StateName(ASCIIToUTF16("B.Y")),
      AlternativeStateNameMap::StateName(ASCIIToUTF16("Bav-aria")),
      AlternativeStateNameMap::StateName(UTF8ToUTF16("amapá")),
      AlternativeStateNameMap::StateName(ASCIIToUTF16("Broen")),
      AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria is in Germany")),
      AlternativeStateNameMap::StateName(ASCIIToUTF16("BA is in Germany"))};
  std::vector<bool> state_data_present = {true,  true,  true,  true,
                                          false, false, false, false};

  alternative_state_name_map_updater_->ProcessLoadedStateFileContentForTesting(
      test_strings, states_data, base::DoNothing());
  AlternativeStateNameMap* alternative_state_name_map =
      AlternativeStateNameMap::GetInstance();
  DCHECK(!alternative_state_name_map->IsLocalisedStateNamesMapEmpty());
  for (size_t i = 0; i < test_strings.size(); i++) {
    SCOPED_TRACE(test_strings[i]);
    EXPECT_EQ(AlternativeStateNameMap::GetCanonicalStateName(
                  "DE", test_strings[i].value()) != base::nullopt,
              state_data_present[i]);
  }
}

// Tests that the AlternativeStateNameMap is populated when
// |StateNameMapUpdater::LoadStatesData()| is called.
TEST_F(AlternativeStateNameMapUpdaterTest, TestLoadStatesData) {
  test::ClearAlternativeStateNameMapForTesting();

  base::WriteFile(GetPath().AppendASCII("DE"),
                  test::CreateStatesProtoAsString());
  WritePathToPref(GetPath());
  CountryToStateNamesListMapping country_to_state_names_list_mapping = {
      {AlternativeStateNameMap::CountryCode("DE"),
       {AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria"))}}};
  base::RunLoop run_loop;
  alternative_state_name_map_updater_->LoadStatesDataForTesting(
      country_to_state_names_list_mapping, autofill_client_.GetPrefs(),
      run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_NE(AlternativeStateNameMap::GetCanonicalStateName(
                "DE", ASCIIToUTF16("Bavaria")),
            base::nullopt);
}

// Tests that there is no insertion in the AlternativeStateNameMap when a
// garbage country code is supplied to the LoadStatesData for which the states
// data file does not exist.
TEST_F(AlternativeStateNameMapUpdaterTest, NoTaskIsPosted) {
  test::ClearAlternativeStateNameMapForTesting();

  base::WriteFile(GetPath().AppendASCII("DE"),
                  test::CreateStatesProtoAsString());
  WritePathToPref(GetPath());

  CountryToStateNamesListMapping country_to_state_names_list_mapping = {
      {AlternativeStateNameMap::CountryCode("DEE"),
       {AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria"))}}};
  base::RunLoop run_loop;
  alternative_state_name_map_updater_->LoadStatesDataForTesting(
      country_to_state_names_list_mapping, autofill_client_.GetPrefs(),
      run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_TRUE(
      AlternativeStateNameMap::GetInstance()->IsLocalisedStateNamesMapEmpty());
}

// Tests that the AlternativeStateNameMap is populated when
// |StateNameMapUpdater::LoadStatesData()| is called and there are UTF8 strings.
TEST_F(AlternativeStateNameMapUpdaterTest, TestLoadStatesDataUTF8) {
  test::ClearAlternativeStateNameMapForTesting();

  base::WriteFile(
      GetPath().AppendASCII("ES"),
      test::CreateStatesProtoAsString(
          "ES", {.canonical_name = "Paraná",
                 .abbreviations = {"PR"},
                 .alternative_names = {"Parana", "State of Parana"}}));
  WritePathToPref(GetPath());

  CountryToStateNamesListMapping country_to_state_names_list_mapping = {
      {AlternativeStateNameMap::CountryCode("ES"),
       {AlternativeStateNameMap::StateName(ASCIIToUTF16("Parana"))}}};

  base::RunLoop run_loop;
  alternative_state_name_map_updater_->LoadStatesDataForTesting(
      country_to_state_names_list_mapping, autofill_client_.GetPrefs(),
      run_loop.QuitClosure());
  run_loop.Run();

  base::Optional<StateEntry> entry1 =
      AlternativeStateNameMap::GetInstance()->GetEntry(
          AlternativeStateNameMap::CountryCode("ES"),
          AlternativeStateNameMap::StateName(UTF8ToUTF16("Paraná")));
  EXPECT_NE(entry1, base::nullopt);
  EXPECT_EQ(entry1->canonical_name(), "Paraná");
  EXPECT_THAT(entry1->abbreviations(),
              testing::UnorderedElementsAreArray({"PR"}));
  EXPECT_THAT(entry1->alternative_names(), testing::UnorderedElementsAreArray(
                                               {"Parana", "State of Parana"}));

  base::Optional<StateEntry> entry2 =
      AlternativeStateNameMap::GetInstance()->GetEntry(
          AlternativeStateNameMap::CountryCode("ES"),
          AlternativeStateNameMap::StateName(UTF8ToUTF16("Parana")));
  EXPECT_NE(entry2, base::nullopt);
  EXPECT_EQ(entry2->canonical_name(), "Paraná");
  EXPECT_THAT(entry2->abbreviations(),
              testing::UnorderedElementsAreArray({"PR"}));
  EXPECT_THAT(entry2->alternative_names(), testing::UnorderedElementsAreArray(
                                               {"Parana", "State of Parana"}));
}

// Tests that the AlternativeStateNameMap is populated when
// |StateNameMapUpdater::LoadStatesData()| is called for states data of
// multiple countries simultaneously.
TEST_F(AlternativeStateNameMapUpdaterTest,
       TestLoadStatesDataOfMultipleCountriesSimultaneously) {
  test::ClearAlternativeStateNameMapForTesting();

  base::WriteFile(GetPath().AppendASCII("DE"),
                  test::CreateStatesProtoAsString());
  base::WriteFile(
      GetPath().AppendASCII("ES"),
      test::CreateStatesProtoAsString(
          "ES", {.canonical_name = "Paraná",
                 .abbreviations = {"PR"},
                 .alternative_names = {"Parana", "State of Parana"}}));
  WritePathToPref(GetPath());

  CountryToStateNamesListMapping country_to_state_names = {
      {AlternativeStateNameMap::CountryCode("ES"),
       {AlternativeStateNameMap::StateName(ASCIIToUTF16("Parana"))}},
      {AlternativeStateNameMap::CountryCode("DE"),
       {AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria"))}}};

  base::RunLoop run_loop;
  alternative_state_name_map_updater_->LoadStatesDataForTesting(
      country_to_state_names, autofill_client_.GetPrefs(),
      run_loop.QuitClosure());
  run_loop.Run();

  base::Optional<StateEntry> entry1 =
      AlternativeStateNameMap::GetInstance()->GetEntry(
          AlternativeStateNameMap::CountryCode("ES"),
          AlternativeStateNameMap::StateName(UTF8ToUTF16("Paraná")));
  EXPECT_NE(entry1, base::nullopt);
  EXPECT_EQ(entry1->canonical_name(), "Paraná");
  EXPECT_THAT(entry1->abbreviations(),
              testing::UnorderedElementsAreArray({"PR"}));
  EXPECT_THAT(entry1->alternative_names(), testing::UnorderedElementsAreArray(
                                               {"Parana", "State of Parana"}));

  base::Optional<StateEntry> entry2 =
      AlternativeStateNameMap::GetInstance()->GetEntry(
          AlternativeStateNameMap::CountryCode("DE"),
          AlternativeStateNameMap::StateName(UTF8ToUTF16("Bavaria")));
  EXPECT_NE(entry2, base::nullopt);
  EXPECT_EQ(entry2->canonical_name(), "Bavaria");
  EXPECT_THAT(entry2->abbreviations(),
              testing::UnorderedElementsAreArray({"BY"}));
  EXPECT_THAT(entry2->alternative_names(),
              testing::UnorderedElementsAreArray({"Bayern"}));
}

// Tests the |StateNameMapUpdater::ContainsState()| functionality.
TEST_F(AlternativeStateNameMapUpdaterTest, ContainsState) {
  EXPECT_TRUE(AlternativeStateNameMapUpdater::ContainsStateForTesting(
      {AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria")),
       AlternativeStateNameMap::StateName(ASCIIToUTF16("Bayern")),
       AlternativeStateNameMap::StateName(ASCIIToUTF16("BY"))},
      AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria"))));
  EXPECT_FALSE(AlternativeStateNameMapUpdater::ContainsStateForTesting(
      {AlternativeStateNameMap::StateName(ASCIIToUTF16("Bavaria")),
       AlternativeStateNameMap::StateName(ASCIIToUTF16("Bayern")),
       AlternativeStateNameMap::StateName(ASCIIToUTF16("BY"))},
      AlternativeStateNameMap::StateName(ASCIIToUTF16("California"))));
}

// Tests that the |AlternativeStateNameMap| is populated with the help of the
// |MockAlternativeStateNameMapUpdater| observer when a new profile is added to
// the PDM.
TEST_F(AlternativeStateNameMapUpdaterTest,
       PopulateAlternativeStateNameUsingObserver) {
  test::ClearAlternativeStateNameMapForTesting();
  WritePathToPref(GetPath());
  base::WriteFile(GetPath().AppendASCII("DE"),
                  test::CreateStatesProtoAsString());

  AutofillProfile profile;
  profile.SetInfo(ADDRESS_HOME_STATE, base::ASCIIToUTF16("Bavaria"), "en-US");
  profile.SetInfo(ADDRESS_HOME_COUNTRY, base::ASCIIToUTF16("DE"), "en-US");

  base::RunLoop run_loop;
  MockAlternativeStateNameMapUpdater mock_alternative_state_name_updater(
      run_loop.QuitClosure(), autofill_client_.GetPrefs(),
      &personal_data_manager_);
  personal_data_manager_.AddObserver(&mock_alternative_state_name_updater);
  personal_data_manager_.AddProfile(profile);
  run_loop.Run();
  personal_data_manager_.RemoveObserver(&mock_alternative_state_name_updater);

  EXPECT_FALSE(
      AlternativeStateNameMap::GetInstance()->IsLocalisedStateNamesMapEmpty());
  EXPECT_NE(AlternativeStateNameMap::GetCanonicalStateName(
                "DE", base::ASCIIToUTF16("Bavaria")),
            AlternativeStateNameMap::CanonicalStateName(
                base::ASCIIToUTF16("Bayern")));
}

}  // namespace autofill
