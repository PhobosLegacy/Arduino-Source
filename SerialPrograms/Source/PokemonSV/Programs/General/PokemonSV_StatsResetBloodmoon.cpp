/*  Stats Reset Bloodmoon
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "CommonFramework/Exceptions/OperationFailedException.h"
#include "CommonFramework/Options/LanguageOCROption.h"
#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonFramework/InferenceInfra/InferenceRoutines.h"
#include "CommonFramework/Tools/StatsTracking.h"
#include "CommonFramework/Tools/VideoResolutionCheck.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "Pokemon/Pokemon_Strings.h"
#include "PokemonSV/PokemonSV_Settings.h"
#include "PokemonSV/Inference/Dialogs/PokemonSV_DialogDetector.h"
#include "PokemonSV/Inference/Battles/PokemonSV_NormalBattleMenus.h"
#include "PokemonSV/Inference/Boxes/PokemonSV_StatsResetChecker.h"
#include "PokemonSV/Inference/Boxes/PokemonSV_BoxDetection.h"
#include "PokemonSV/Inference/Boxes/PokemonSV_IVCheckerReader.h"
#include "PokemonSV/Inference/Battles/PokemonSV_TeraBattleMenus.h"
#include "PokemonSV/Inference/Overworld/PokemonSV_OverworldDetector.h"
#include "PokemonSV/Programs/PokemonSV_GameEntry.h"
#include "PokemonSV/Programs/PokemonSV_Navigation.h"
#include "PokemonSV/Programs/PokemonSV_BasicCatcher.h"
#include "PokemonSV/Programs/Boxes/PokemonSV_BoxRoutines.h"
#include "PokemonSV_StatsResetBloodmoon.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonSV{

using namespace Pokemon;
 
StatsResetBloodmoon_Descriptor::StatsResetBloodmoon_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonSV:StatsResetBloodmoon",
        STRING_POKEMON + " SV", "Stats Reset - Bloodmoon Ursaluna",
        "ComputerControl/blob/master/Wiki/Programs/PokemonSV/StatsResetBloodmoon.md",
        "Repeatedly catch Bloodmoon Ursaluna until you get the stats you want.",
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS,
        PABotBaseLevel::PABOTBASE_12KB
    )
{}
struct StatsResetBloodmoon_Descriptor::Stats : public StatsTracker {
    Stats()
        : resets(m_stats["Resets"])
        , catches(m_stats["Catches"])
        , matches(m_stats["Matches"])
        , errors(m_stats["Errors"])
    {
        m_display_order.emplace_back("Resets");
        m_display_order.emplace_back("Catches");
        m_display_order.emplace_back("Matches");
        m_display_order.emplace_back("Errors", true);
    }
    std::atomic<uint64_t>& resets;
    std::atomic<uint64_t>& catches;
    std::atomic<uint64_t>& matches;
    std::atomic<uint64_t>& errors;
};
std::unique_ptr<StatsTracker> StatsResetBloodmoon_Descriptor::make_stats() const {
    return std::unique_ptr<StatsTracker>(new Stats());
}
StatsResetBloodmoon::StatsResetBloodmoon()
    : LANGUAGE(
        "<b>Game Language:</b><br>This field is required so we can read IVs.",
        IV_READER().languages(),
        LockWhileRunning::LOCKED,
        true
    )
    , BALL_SELECT(
        "<b>Ball Select:</b>",
        LockWhileRunning::UNLOCKED,
        "poke-ball"
    )
    , TRY_TO_TERASTILLIZE(
        "<b>Use Terastillization:</b><br>Tera at the start of battle.",
        LockWhileRunning::UNLOCKED,
        false
    )
    , GO_HOME_WHEN_DONE(false)
    , NOTIFICATION_STATUS_UPDATE("Status Update", true, false, std::chrono::seconds(3600))
    , NOTIFICATIONS({
        &NOTIFICATION_STATUS_UPDATE,
        & NOTIFICATION_PROGRAM_FINISH,
        & NOTIFICATION_ERROR_FATAL,
    })
{
    PA_ADD_OPTION(LANGUAGE);
    PA_ADD_OPTION(BALL_SELECT);
    PA_ADD_OPTION(TRY_TO_TERASTILLIZE);
    PA_ADD_OPTION(FILTERS);
    PA_ADD_OPTION(GO_HOME_WHEN_DONE);
    PA_ADD_OPTION(NOTIFICATIONS);
}
void StatsResetBloodmoon::enter_battle(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {

    AdvanceDialogWatcher advance_detector(COLOR_YELLOW);
    PromptDialogWatcher prompt_detector(COLOR_YELLOW);
    NormalBattleMenuWatcher battle_menu(COLOR_YELLOW);

    //Initiate dialog with Perrin
    pbf_press_button(context, BUTTON_A, 10, 50);
    int ret = wait_until(env.console, context, Milliseconds(4000), { advance_detector });
    if (ret == 0) {
        env.log("Dialog detected.");
    }
    else {
        env.log("Dialog not detected.");
    }
    //Yes, ready
    pbf_mash_button(context, BUTTON_A, 400);
    context.wait_for_all_requests();

    //Mash B until next dialog select
    int retPrompt = run_until(
        env.console, context,
        [](BotBaseContext& context) {
            pbf_mash_button(context, BUTTON_B, 10000);
        },
        { prompt_detector }
        );
    if (retPrompt != 0) {
        env.log("Failed to detect prompt dialog!", COLOR_RED);
    }
    else {
        env.log("Detected prompt dialog.");
    }
    context.wait_for_all_requests();

    //Pick an option to continue
    pbf_mash_button(context, BUTTON_A, 400);
    context.wait_for_all_requests();

    //Mash B until next dialog select (again)
    //PromptDialogWatcher prompt_detector2(COLOR_YELLOW);
    int retPrompt2 = run_until(
        env.console, context,
        [](BotBaseContext& context) {
            pbf_mash_button(context, BUTTON_B, 10000);
        },
        { prompt_detector }
        );
    if (retPrompt2 != 0) {
        env.log("Failed to detect prompt (again) dialog!", COLOR_RED);
    }
    else {
        env.log("Detected prompt (again) dialog.");
    }
    context.wait_for_all_requests();

    //Pick an option to continue
    pbf_mash_button(context, BUTTON_A, 400);
    context.wait_for_all_requests();

    //Now keep going until the battle starts
    int ret_battle = run_until(
        env.console, context,
        [](BotBaseContext& context) {
            pbf_mash_button(context, BUTTON_B, 10000);
        },
        { battle_menu }
        );
    if (ret_battle != 0) {
        env.log("Failed to detect battle start!", COLOR_RED);
    }
    else {
        env.log("Battle started.");
    }
    context.wait_for_all_requests();
}

bool StatsResetBloodmoon::run_battle(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {
    StatsResetBloodmoon_Descriptor::Stats& stats = env.current_stats<StatsResetBloodmoon_Descriptor::Stats>();

    //Assuming the player has a charged orb
    if (TRY_TO_TERASTILLIZE) {
        env.log("Attempting to terastillize.");
        //Open move menu
        pbf_press_button(context, BUTTON_A, 10, 50);
        pbf_wait(context, 100);
        context.wait_for_all_requests();

        pbf_press_button(context, BUTTON_R, 20, 50);
        pbf_press_button(context, BUTTON_A, 10, 50);
    }

    //Repeatedly use first attack
    TeraCatchWatcher catch_menu(COLOR_BLUE);
    AdvanceDialogWatcher lost(COLOR_YELLOW);
    WallClock start = current_time();
    uint8_t switch_party_slot = 1;

    int ret = run_until(
        env.console, context,
        [&](BotBaseContext& context) {
            while(true) {
                if (current_time() - start > std::chrono::minutes(5)) {
                    env.log("Timed out during battle after 5 minutes.", COLOR_RED);
                    stats.errors++;
                    env.update_stats();
                    throw OperationFailedException(
                        ErrorReport::SEND_ERROR_REPORT, env.console,
                        "Timed out during battle after 5 minutes.",
                        true
                    );
                }

                NormalBattleMenuWatcher battle_menu(COLOR_MAGENTA);
                SwapMenuWatcher fainted(COLOR_RED);
                context.wait_for_all_requests();

                int ret = wait_until(
                    env.console, context,
                    std::chrono::seconds(90),
                    { battle_menu, fainted }
                );
                switch (ret) {
                case 0:
                    env.log("Detected battle menu. Pressing A to attack...");
                    pbf_mash_button(context, BUTTON_A, 3 * TICKS_PER_SECOND);
                    context.wait_for_all_requests();
                    break;
                case 1:
                    env.log("Detected fainted Pokemon. Switching to next living Pokemon...");
                    if (fainted.move_to_slot(env.console, context, switch_party_slot)) {
                        pbf_mash_button(context, BUTTON_A, 3 * TICKS_PER_SECOND);
                        context.wait_for_all_requests();
                        switch_party_slot++;
                    }
                    break;
                default:
                    env.log("Timed out during battle. Stuck, crashed, or took more than 90 seconds for a turn.", COLOR_RED);
                    stats.errors++;
                    env.update_stats();
                    throw OperationFailedException(
                        ErrorReport::SEND_ERROR_REPORT, env.console,
                        "Timed out during battle. Stuck, crashed, or took more than 90 seconds for a turn.",
                        true
                    );
                }
            }
        },
        { catch_menu, lost }
        );
    if (ret == 0) {
        env.log("Catch prompt detected.");

        pbf_press_button(context, BUTTON_A, 20, 150);
        context.wait_for_all_requests();

        BattleBallReader reader(env.console, LANGUAGE);
        int quantity = move_to_ball(reader, env.console, context, BALL_SELECT.slug());
        if (quantity == 0) {
            throw OperationFailedException(
                ErrorReport::SEND_ERROR_REPORT, env.console,
                "Unable to find appropriate ball. Did you run out?",
                true
            );
        }
        if (quantity < 0) {
            stats.errors++;
            env.update_stats();
            env.log("Unable to read ball quantity.", COLOR_RED);
        }
        pbf_mash_button(context, BUTTON_A, 125);
        context.wait_for_all_requests();

        stats.catches++;
        env.update_stats();
        env.log("Ursaluna caught.");
    }
    else {
        env.log("Battle against Ursaluna lost.", COLOR_RED);
        env.update_stats();
        send_program_status_notification(
            env, NOTIFICATION_STATUS_UPDATE,
            "Battle against Ursaluna lost."
        );

        return false;
    }
    return true;
}

bool StatsResetBloodmoon::check_stats(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {
    StatsResetBloodmoon_Descriptor::Stats& stats = env.current_stats<StatsResetBloodmoon_Descriptor::Stats>();
    bool match = false;

    //Open box
    enter_box_system_from_overworld(env.program_info(), env.console, context);
    context.wait_for(std::chrono::milliseconds(400));

    if (check_empty_slots_in_party(env.program_info(), env.console, context) != 0) {
        //Is this even possible for Ursaluna?
        env.console.log("One or more empty slots in party. Ursaluna was not caught.");
        send_program_status_notification(
            env, NOTIFICATION_STATUS_UPDATE,
            "One or more empty slots in party. Ursaluna was not caught."
        );
    }
    else {
        //Navigate to last party slot
        move_box_cursor(env.program_info(), env.console, context, BoxCursorLocation::PARTY, 5, 0);

        //Check the IVs of the newly caught Pokemon - *must be on IV panel*
        EggHatchAction action = EggHatchAction::Keep;
        check_stats_reset_info(env.console, context, LANGUAGE, FILTERS, action);

        switch (action) {
        case EggHatchAction::StopProgram:
            match = true;
            env.console.log("Match found!");
            stats.matches++;
            env.update_stats();
            send_program_status_notification(
                env, NOTIFICATION_PROGRAM_FINISH,
                "Match found!"
            );
            break;
        case EggHatchAction::Release:
            match = false;
            env.console.log("Stats did not match table settings.");
            send_program_status_notification(
                env, NOTIFICATION_STATUS_UPDATE,
                "Stats did not match table settings."
            );
            break;
        default:
            env.console.log("Invalid state.");
            stats.errors++;
            env.update_stats();
            throw OperationFailedException(
                ErrorReport::SEND_ERROR_REPORT, env.console,
                "Invalid state.",
                true
            );
        }
    }

    return match;
}

void StatsResetBloodmoon::program(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {
    assert_16_9_720p_min(env.logger(), env.console);
    StatsResetBloodmoon_Descriptor::Stats& stats = env.current_stats<StatsResetBloodmoon_Descriptor::Stats>();

    /*
    * Autosave: Off
    * 5 pokemon in party so that last slot is free.
    * Start standing in front of Perrin.
    * Must be able to reliably kill Ursaluna by spamming your first move.
    * Tested using Walking Wake w/Hydro Stream, two turn kill.
    */

    bool stats_matched = false;
    while (!stats_matched){
        enter_battle(env, context);
        bool battle_won = run_battle(env, context);
        if (battle_won) {
            //Clear out dialog until we're free
            OverworldWatcher overworld(COLOR_YELLOW);
            int retOverworld = run_until(
                env.console, context,
                [](BotBaseContext& context) {
                    pbf_mash_button(context, BUTTON_B, 10000);
                },
                { overworld }
                );
            if (retOverworld != 0) {
                env.log("Failed to detect overworld after catching.", COLOR_RED);
            }
            else {
                env.log("Detected overworld.");
            }
            context.wait_for_all_requests();
            stats_matched = check_stats(env, context);
        }

        if (!battle_won || !stats_matched) {
            //Reset
            stats.resets++;
            env.update_stats();
            send_program_status_notification(
                env, NOTIFICATION_STATUS_UPDATE,
                "Resetting game."
            );
            pbf_press_button(context, BUTTON_HOME, 20, GameSettings::instance().GAME_TO_HOME_DELAY);
            reset_game_from_home(env.program_info(), env.console, context, 5 * TICKS_PER_SECOND);
        }
    }
    env.update_stats();
    GO_HOME_WHEN_DONE.run_end_of_program(context);
    send_program_finished_notification(env, NOTIFICATION_PROGRAM_FINISH);
}
    
}
}
}