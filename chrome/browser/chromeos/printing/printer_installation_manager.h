// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CHROME_BROWSER_CHROMEOS_PRINTING_PRINTER_INSTALLATION_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_PRINTING_PRINTER_INSTALLATION_MANAGER_H_

namespace chromeos {

class Printer;

// Interface that exposes methods for tracking the installation of a printer.
class PrinterInstallationManager {
 public:
  virtual ~PrinterInstallationManager() = default;

  // Record that the given printers has been installed in CUPS for usage.
  // Parameter |is_automatic| should be set to true if the printer was
  // saved automatically (without requesting additional information
  // from the user).
  virtual void PrinterInstalled(const Printer& printer, bool is_automatic) = 0;

  // Returns true if |printer| is currently installed in CUPS with this
  // configuration.
  virtual bool IsPrinterInstalled(const Printer& printer) const = 0;

  // Record that a requested printer installation failed because the printer
  // is not autoconfigurable (does not meet IPP Everywhere requirements).
  // This results in shifting the printer from automatic to discovered class.
  virtual void PrinterIsNotAutoconfigurable(const Printer& printer) = 0;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_PRINTING_PRINTER_INSTALLATION_MANAGER_H_
