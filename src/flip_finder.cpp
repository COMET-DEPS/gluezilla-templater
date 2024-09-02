/**
 * @file     flip_finder.cpp
 *
 * @brief    Handles the creation of experiments and repetitions of such.
 *
 */

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "db.h"
#include "logging.h"

#include "flip_finder.h"
#include "temperature_controller.h"

/**
 * @brief    Executes the hammering iteration with given repetition.
 *
 * @param    iter_algorithm      The iteration algorithm used for hammering.
 * @param    target_temperature  The set target temperature.
 */
void FlipFinder::repetition_loop(std::function<void()> iter_algorithm,
                                 int64_t target_temperature) {
  for (uint32_t rep = 0; rep < config.experiment_repetitions; ++rep) {
#ifdef USE_DB
    log_info("Experiment ID: ",
             db->start_experiment(config.aggressor_rows, config.hammer_count,
                                  target_temperature,
                                  config.experiment_comment));
#endif

    do_exit = false;
    if (config.test_max_time.count() > 0) {
      const std::chrono::seconds &duration = config.test_max_time;
      std::thread([duration] {
        std::this_thread::sleep_for(duration);
        do_exit = true;
      }).detach();
    }

    iter_algorithm();

#ifdef USE_DB
    db->end_experiment();
#endif
  }
}

/**
 * @brief    Creates the experiments for hammering.
 *
 * @param    iter_algorithm      The iteration algorithm used for hammering.
 */
void FlipFinder::experiment_loop(std::function<void()> iter_algorithm) {
  std::signal(SIGINT, [](int) { do_exit = true; });

  if (!config.target_temps.empty()) {
    log_info("Using Temperature Controller...");

    if (!temperature_controller.connect()) {
      log_error_and_exit("Could not open device '", config.device, "'");
    }

    for (auto target_temperature : config.target_temps) {
      temperature_controller.set_target_temperature(target_temperature);
      auto actual_temperature = temperature_controller.get_actual_temperature();

      using namespace std::chrono;
      auto start_time = steady_clock::now();

      do_exit = false;
      while (actual_temperature != target_temperature &&
             steady_clock::now() - start_time < config.timeout && !do_exit) {
        std::this_thread::sleep_for(1s);
        actual_temperature = temperature_controller.get_actual_temperature();
      }

      if (do_exit) {
        log_trace("do_exit == true");
        temperature_controller.set_target_temperature(
            20); // stop heatpads from heating after testruns are cancelled,
                 // aborted or finished
        exit(EXIT_SUCCESS);
      }

      if (actual_temperature != target_temperature) {
        temperature_controller.set_target_temperature(20);
        log_error_and_exit(
            "Timeout: could not reach target temperature within ",
            config.timeout.count(), " seconds");
      }

      repetition_loop(iter_algorithm, target_temperature);
    }

    temperature_controller.set_target_temperature(20);

  } else {
    repetition_loop(iter_algorithm);
  }

  std::signal(SIGINT, SIG_DFL);
}