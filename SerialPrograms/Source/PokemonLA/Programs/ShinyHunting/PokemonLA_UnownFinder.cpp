/*  Unown Finder
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "Common/Cpp/Exceptions.h"
#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonFramework/Tools/StatsTracking.h"
#include "CommonFramework/InferenceInfra/InferenceRoutines.h"
#include "CommonFramework/Inference/BlackScreenDetector.h"
#include "NintendoSwitch/NintendoSwitch_Settings.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "PokemonLA/PokemonLA_Settings.h"
#include "PokemonLA/Inference/Objects/PokemonLA_ButtonDetector.h"
#include "PokemonLA/Inference/PokemonLA_MapDetector.h"
#include "PokemonLA/Inference/PokemonLA_DialogDetector.h"
#include "PokemonLA/Inference/PokemonLA_OverworldDetector.h"
#include "PokemonLA/Inference/PokemonLA_UnderAttackDetector.h"
#include "PokemonLA/Inference/Sounds/PokemonLA_ShinySoundDetector.h"
#include "PokemonLA/Programs/PokemonLA_MountChange.h"
#include "PokemonLA/Programs/PokemonLA_GameEntry.h"
#include "PokemonLA/Programs/PokemonLA_RegionNavigation.h"
#include "PokemonLA/Programs/ShinyHunting/PokemonLA_UnownFinder.h"


namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLA{


UnownFinder_Descriptor::UnownFinder_Descriptor()
    : RunnableSwitchProgramDescriptor(
        "PokemonLA:UnownFinder",
        STRING_POKEMON + " LA", "Unown Hunter",
        "ComputerControl/blob/master/Wiki/Programs/PokemonLA/UnownHunter.md",
        "Constantly reset to find a Shiny Unown or any Shiny in the path.",
        FeedbackType::REQUIRED, false,
        PABotBaseLevel::PABOTBASE_12KB
    )
{}


UnownFinder::UnownFinder(const UnownFinder_Descriptor& descriptor)
    : SingleSwitchProgramInstance(descriptor)
    , SHINY_DETECTED_ENROUTE(
        "Enroute Shiny Action",
        "This applies if a shiny is detected while enroute to the ruins.",
        "0 * TICKS_PER_SECOND"
    )
    , SHINY_DETECTED_DESTINATION(
        "Destination Shiny Action",
        "This applies if a shiny is detected inside the ruins.",
        "0 * TICKS_PER_SECOND"
    )
    , NOTIFICATION_STATUS("Status Update", true, false, std::chrono::seconds(3600))
    , NOTIFICATIONS({
        &NOTIFICATION_STATUS,
        &SHINY_DETECTED_DESTINATION.NOTIFICATIONS,
        &NOTIFICATION_PROGRAM_FINISH,
        &NOTIFICATION_ERROR_FATAL,
    })
{
    PA_ADD_STATIC(SHINY_REQUIRES_AUDIO);
    PA_ADD_OPTION(SHINY_DETECTED_ENROUTE);
    PA_ADD_OPTION(SHINY_DETECTED_DESTINATION);
    PA_ADD_OPTION(NOTIFICATIONS);
}


class UnownFinder::Stats : public StatsTracker, public ShinyStatIncrementer{
public:
    Stats()
        : attempts(m_stats["Attempts"])
        , errors(m_stats["Errors"])
        , shinies(m_stats["Shinies"])
    {
        m_display_order.emplace_back("Attempts");
        m_display_order.emplace_back("Errors", true);
        m_display_order.emplace_back("Shinies", true);
    }
    virtual void add_shiny() override{
        shinies++;
    }

    std::atomic<uint64_t>& attempts;
    std::atomic<uint64_t>& errors;
    std::atomic<uint64_t>& shinies;
};

std::unique_ptr<StatsTracker> UnownFinder::make_stats() const{
    return std::unique_ptr<StatsTracker>(new Stats());
}

void ruins_entrance_route(BotBaseContext& context){
    pbf_wait(context, (uint16_t)(0.5 * TICKS_PER_SECOND));
    pbf_move_left_joystick(context, 139, 120, 10, 10);
    pbf_wait(context, (uint16_t)(1.3 * TICKS_PER_SECOND));

    pbf_press_button(context, BUTTON_B, (uint16_t)(9.5 * TICKS_PER_SECOND), 10);
    pbf_wait(context, (uint16_t)(0.8 * TICKS_PER_SECOND));
    pbf_move_left_joystick(context, 110, 90, 20, 10);

    pbf_press_dpad(context, DPAD_LEFT, 10, 10);
    pbf_press_button(context, BUTTON_PLUS, 10, 10);
}

void enter_ruins(BotBaseContext& context){
    pbf_press_button(context, BUTTON_B, (uint16_t)(4 * TICKS_PER_SECOND), 10);
    pbf_wait(context, (uint16_t)(1.5 * TICKS_PER_SECOND));
    pbf_move_left_joystick(context, 128, 255, 10, 0);
    pbf_press_button(context, BUTTON_B, (uint16_t)(2 * TICKS_PER_SECOND), 10);
}


void UnownFinder::run_iteration(SingleSwitchProgramEnvironment& env, BotBaseContext& context){
    Stats& stats = env.stats<Stats>();

    stats.attempts++;

    goto_camp_from_jubilife(env, env.console, context, TravelLocations::instance().Mirelands_Mirelands);

    change_mount(env.console, context, MountState::BRAVIARY_ON);

    // Start path
    env.console.log("Beginning Shiny Detection...");

    //Head to ruins
    {
        float shiny_coefficient = 1.0;
        ShinySoundDetector shiny_detector(env.console, [&](float error_coefficient) -> bool{
            //  Warning: This callback will be run from a different thread than this function.
            stats.shinies++;
            shiny_coefficient = error_coefficient;
            return on_shiny_callback(env, env.console, SHINY_DETECTED_ENROUTE, error_coefficient);
        });

        int ret = run_until(
            env.console, context,
            [](BotBaseContext& context){
               ruins_entrance_route(context);
            },
            {{shiny_detector}}
        );
        if (ret == 0){
            on_shiny_sound(env, env.console, context, SHINY_DETECTED_ENROUTE, shiny_coefficient);
        }
    }

    // Enter ruins
    {
        float shiny_coefficient = 1.0;
        ShinySoundDetector shiny_detector(env.console, [&](float error_coefficient) -> bool{
            //  Warning: This callback will be run from a different thread than this function.
            stats.shinies++;
            shiny_coefficient = error_coefficient;
            return on_shiny_callback(env, env.console, SHINY_DETECTED_DESTINATION, error_coefficient);
        });

        int ret = run_until(env.console, context,
            [](BotBaseContext& context){
               enter_ruins(context);
            },
            {{shiny_detector}}
        );
        if (ret == 0){
            on_shiny_sound(env, env.console, context, SHINY_DETECTED_DESTINATION, shiny_coefficient);
        }
    }

    env.console.log("No shiny detected, returning to Jubilife!");
    goto_camp_from_overworld(env, env.console, context);
    pbf_press_dpad(context, DPAD_RIGHT, 10, 10);
    goto_professor(env.console, context, Camp::MIRELANDS_MIRELANDS);
    from_professor_return_to_jubilife(env, env.console, context);
}


void UnownFinder::program(SingleSwitchProgramEnvironment& env, BotBaseContext& context){
    Stats& stats = env.stats<Stats>();

    //  Connect the controller.
    pbf_press_button(context, BUTTON_LCLICK, 5, 5);

    while (true){
        env.update_stats();
        send_program_status_notification(
            env.logger(), NOTIFICATION_STATUS,
            env.program_info(),
            "",
            stats.to_str()
        );
        try{
            run_iteration(env, context);
        }catch (OperationFailedException&){
            stats.errors++;
            pbf_press_button(context, BUTTON_HOME, 20, GameSettings::instance().GAME_TO_HOME_DELAY);
            reset_game_from_home(env, env.console, context, ConsoleSettings::instance().TOLERATE_SYSTEM_UPDATE_MENU_FAST);
        }
    }

    env.update_stats();
    send_program_finished_notification(
        env.logger(), NOTIFICATION_PROGRAM_FINISH,
        env.program_info(),
        "",
        stats.to_str()
    );
}



}
}
}
