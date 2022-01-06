// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_DIAGNOSTICS_UI_BACKEND_ROUTINE_LOG_H_
#define ASH_WEBUI_DIAGNOSTICS_UI_BACKEND_ROUTINE_LOG_H_

#include <memory>
#include <string>

#include "ash/webui/diagnostics_ui/backend/async_log.h"
#include "ash/webui/diagnostics_ui/mojom/system_routine_controller.mojom.h"
#include "base/containers/flat_map.h"
#include "base/files/file_path.h"

namespace ash {
namespace diagnostics {

// RoutineLog is used to record the status and outcome of Diagnostics Routines.
// Each time `LogRoutineStarted()` or `LogRoutineCompleted()` is called, a new
// line is add to the log for the routine's category. `log_base_path`
// is the directory used to write the category logs which have the name
// "diagnostics_routines_{category_name}.log".
class RoutineLog {
 public:
  explicit RoutineLog(const base::FilePath& log_base_path);
  ~RoutineLog();

  RoutineLog(const RoutineLog&) = delete;
  RoutineLog& operator=(const RoutineLog&) = delete;

  // LogRoutine* functions schedule a task using sequence_task_runner_ to add an
  // entry in the routine log file. Tasks are run on blockable, sequenced task
  // runner to support I/O operations.
  void LogRoutineStarted(mojom::RoutineType type);
  void LogRoutineCompleted(mojom::RoutineType type,
                           mojom::StandardRoutineResult result);
  void LogRoutineCancelled(mojom::RoutineType type);

  // Returns the current RoutineLog as a string.
  std::string GetContentsForCategory(const std::string& category) const;

 private:
  // Append `text` to the category corresponding to `type`.
  void Append(mojom::RoutineType type, const std::string& text);

  // Get the path to the log file for `category`.
  base::FilePath GetCategoryLogFilePath(const std::string& category);

  // The base directory for storing logs.
  const base::FilePath log_base_path_;

  // A map of log files where the key is the category.
  base::flat_map<std::string, std::unique_ptr<AsyncLog>> logs_;
};

}  // namespace diagnostics
}  // namespace ash

#endif  // ASH_WEBUI_DIAGNOSTICS_UI_BACKEND_ROUTINE_LOG_H_