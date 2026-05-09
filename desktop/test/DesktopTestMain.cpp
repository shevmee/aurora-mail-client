#include <gtest/gtest.h>

#include <QCoreApplication>

// QCoreApplication is required for QString static factories that touch the
// codec/locale subsystem, for QJsonDocument::fromJson(), QFile, QDateTime,
// QTemporaryFile, and QSqlDatabase. We therefore start one before any test
// runs and tear it down at process exit.
//
// We intentionally do NOT use QApplication (which is GUI) -- the non-UI
// Core code under test does not touch QWidget, and instantiating QApplication
// inside a headless test environment fails on macOS without an active
// window-server connection.
int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
