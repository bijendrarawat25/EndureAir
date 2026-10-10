#ifndef BIJENDRA_DBMS_H
#define BIJENDRA_DBMS_H

#include <string>
#include <vector>
#include <mutex>
#include <map>

// ========================================================
// Bijendra's Data Models (Database Schema)
// ========================================================
constexpr int DB_TOTAL_SEATS = 10;

struct DbSeat {
    int seatNumber;
    bool isBooked;
    int bookedByUserId;
    std::string passengerName;
    std::mutex seatLock; // Row-level lock to prevent double booking
};

struct BookingRecord {
    int bookingId;
    int txnId;
    int userId;
    std::string passengerName;
    int seatNumber;
    int meals;
    int baggageKg;
    std::string status; // "COMMITTED", "ROLLED_BACK", "IN_FLIGHT"
};

enum LogRecordType { LOG_START, LOG_UPDATE, LOG_COMMIT, LOG_ABORT, LOG_CHECKPOINT };

struct WalEntry {
    int txnId;
    LogRecordType type;
    std::string item;
    std::string oldValue;
    std::string newValue;
};

// ========================================================
// Bijendra's Module: Write-Ahead Logging (WAL) Engine
// ========================================================
class WriteAheadLogger {
private:
    std::string logFilePath;
    std::mutex walMutex;

public:
    WriteAheadLogger(const std::string& path = "wal.log");

    void logRecord(int txnId, LogRecordType type, 
                   const std::string& item = "", 
                   const std::string& oldVal = "", 
                   const std::string& newVal = "");

    std::vector<WalEntry> parseLogFile();
    void truncateLog();
    void displayRawLogs();
};

// ========================================================
// Bijendra's Module: Database & ACID Transaction Engine
// ========================================================
class EndureAirDatabase {
private:
    DbSeat seats[DB_TOTAL_SEATS];
    int availableMeals;
    int availableBaggageKg;
    std::vector<BookingRecord> bookingsTable;
    std::mutex dbGlobalMutex;

public:
    WriteAheadLogger wal;

    EndureAirDatabase(int initialMeals = 15, int initialBaggage = 300);

    // ACID Transaction Execution with Row-Level Locking
    bool executeBookingTransaction(int txnId, int userId, const std::string& userName,
                                   int seatNum, int meals, int baggageKg,
                                   int crashStage);

    // Bijendra's Crash Recovery: Two/Three-Pass Undo-Redo Algorithm
    void performCrashRecovery();

    // Visualisation & SQL Audit queries
    void printSeatMap();
    void printAuditReport();
    bool isSeatBooked(int seatNum);
};

#endif // BIJENDRA_DBMS_H
