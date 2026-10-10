#include "bijendra_dbms.h"
#include "aryan_os.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

// ========================================================
// Write-Ahead Logger Implementation
// ========================================================
WriteAheadLogger::WriteAheadLogger(const std::string& path) : logFilePath(path) {}

void WriteAheadLogger::logRecord(int txnId, LogRecordType type, 
                                const std::string& item, 
                                const std::string& oldVal, 
                                const std::string& newVal) {
    std::lock_guard<std::mutex> lock(walMutex);
    std::ofstream file(logFilePath, std::ios::app);
    if (!file.is_open()) return;

    std::string typeStr;
    switch (type) {
        case LOG_START: typeStr = "START"; break;
        case LOG_UPDATE: typeStr = "UPDATE"; break;
        case LOG_COMMIT: typeStr = "COMMIT"; break;
        case LOG_ABORT: typeStr = "ABORT"; break;
        case LOG_CHECKPOINT: typeStr = "CHECKPOINT"; break;
    }

    file << txnId << "," << typeStr << "," << item << "," << oldVal << "," << newVal << "\n";
    file.flush(); // Enforce durability onto persistent storage
}

std::vector<WalEntry> WriteAheadLogger::parseLogFile() {
    std::lock_guard<std::mutex> lock(walMutex);
    std::vector<WalEntry> entries;
    std::ifstream file(logFilePath);
    if (!file.is_open()) return entries;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string txnStr, typeStr, item, oldVal, newVal;
        std::getline(ss, txnStr, ',');
        std::getline(ss, typeStr, ',');
        std::getline(ss, item, ',');
        std::getline(ss, oldVal, ',');
        std::getline(ss, newVal, ',');

        WalEntry entry;
        entry.txnId = std::stoi(txnStr);
        entry.item = item;
        entry.oldValue = oldVal;
        entry.newValue = newVal;

        if (typeStr == "START") entry.type = LOG_START;
        else if (typeStr == "UPDATE") entry.type = LOG_UPDATE;
        else if (typeStr == "COMMIT") entry.type = LOG_COMMIT;
        else if (typeStr == "ABORT") entry.type = LOG_ABORT;
        else if (typeStr == "CHECKPOINT") entry.type = LOG_CHECKPOINT;

        entries.push_back(entry);
    }
    return entries;
}

void WriteAheadLogger::truncateLog() {
    std::lock_guard<std::mutex> lock(walMutex);
    std::ofstream file(logFilePath, std::ios::trunc);
}

void WriteAheadLogger::displayRawLogs() {
    std::vector<WalEntry> entries = parseLogFile();
    std::cout << "\n================ [BIJENDRA DBMS] WRITE-AHEAD LOG (WAL) DUMP ================\n";
    if (entries.empty()) {
        std::cout << ">> Log file is empty.\n";
    } else {
        std::cout << std::left << std::setw(8) << "TxnID"
                  << std::setw(12) << "Type"
                  << std::setw(15) << "Target Item"
                  << std::setw(15) << "Old Value"
                  << std::setw(15) << "New Value\n";
        std::cout << "----------------------------------------------------------------------------\n";
        for (const auto& e : entries) {
            std::string tStr = (e.type == LOG_START) ? "START" :
                               (e.type == LOG_UPDATE) ? "UPDATE" :
                               (e.type == LOG_COMMIT) ? "COMMIT" :
                               (e.type == LOG_ABORT) ? "ABORT" : "CHECKPOINT";
            std::cout << std::left << std::setw(8) << e.txnId
                      << std::setw(12) << tStr
                      << std::setw(15) << e.item
                      << std::setw(15) << e.oldValue
                      << std::setw(15) << e.newValue << "\n";
        }
    }
    std::cout << "============================================================================\n\n";
}

// ========================================================
// Database & Transaction Engine Implementation
// ========================================================
EndureAirDatabase::EndureAirDatabase(int initialMeals, int initialBaggage)
    : availableMeals(initialMeals), availableBaggageKg(initialBaggage), wal("wal.log") {
    for (int i = 0; i < DB_TOTAL_SEATS; ++i) {
        seats[i].seatNumber = i + 1;
        seats[i].isBooked = false;
        seats[i].bookedByUserId = -1;
        seats[i].passengerName = "AVAILABLE";
    }
}

bool EndureAirDatabase::executeBookingTransaction(int txnId, int userId, const std::string& userName,
                                                  int seatNum, int meals, int baggageKg,
                                                  int crashStage) {
    if (seatNum < 1 || seatNum > DB_TOTAL_SEATS) {
        std::cout << "[Bijendra DBMS] Error: Invalid seat number #" << seatNum << "\n";
        return false;
    }

    DbSeat& targetSeat = seats[seatNum - 1];

    // STEP 1: Row-Level Locking (Prevents double booking race conditions)
    std::unique_lock<std::mutex> rowLock(targetSeat.seatLock);

    if (targetSeat.isBooked) {
        std::cout << "[Bijendra DBMS] Lock Conflict: Seat #" << seatNum 
                  << " is ALREADY BOOKED by User #" << targetSeat.bookedByUserId << "!\n";
        return false;
    }

    std::cout << "[Bijendra DBMS] Transaction #" << txnId << ": Acquired Row-Level Lock on Seat #" << seatNum << ".\n";

    // STEP 2: WAL Record: START transaction
    wal.logRecord(txnId, LOG_START);

    // STEP 3: WAL Record: UPDATE before applying changes (Write-Ahead guarantee)
    wal.logRecord(txnId, LOG_UPDATE, "SEAT_" + std::to_string(seatNum), "AVAILABLE", userName);
    wal.logRecord(txnId, LOG_UPDATE, "MEALS", std::to_string(availableMeals), std::to_string(availableMeals - meals));
    wal.logRecord(txnId, LOG_UPDATE, "BAGGAGE", std::to_string(availableBaggageKg), std::to_string(availableBaggageKg - baggageKg));

    // SIMULATED CRASH STAGE 1: Crash immediately after WAL logging but BEFORE database update
    if (crashStage == 1) {
        triggerSimulatedCrash(101, "Crash Stage 1: Written to WAL, but DB state untouched (Mid-Transaction crash)");
    }

    // STEP 4: Modify Database State in Memory
    targetSeat.isBooked = true;
    targetSeat.bookedByUserId = userId;
    targetSeat.passengerName = userName;
    availableMeals -= meals;
    availableBaggageKg -= baggageKg;

    // SIMULATED CRASH STAGE 2: Crash after in-memory state update but BEFORE writing COMMIT record to WAL
    if (crashStage == 2) {
        triggerSimulatedCrash(102, "Crash Stage 2: Updated in-memory, but crash happened before COMMIT log was written");
    }

    // STEP 5: WAL Record: COMMIT transaction
    wal.logRecord(txnId, LOG_COMMIT);

    // Record in Bookings Table
    BookingRecord record = {
        static_cast<int>(bookingsTable.size() + 1),
        txnId,
        userId,
        userName,
        seatNum,
        meals,
        baggageKg,
        "COMMITTED"
    };
    bookingsTable.push_back(record);

    std::cout << "[Bijendra DBMS] Transaction #" << txnId << ": COMMITTED successfully with full ACID compliance.\n";
    return true;
}

// Bijendra's Undo / Redo Crash Recovery Algorithm
void EndureAirDatabase::performCrashRecovery() {
    std::cout << "\n================ [BIJENDRA DBMS] CRASH RECOVERY ALGORITHM ================\n";
    std::vector<WalEntry> logs = wal.parseLogFile();
    if (logs.empty()) {
        std::cout << ">> WAL is empty. Database state is consistent.\n";
        std::cout << "=========================================================================\n\n";
        return;
    }

    std::map<int, bool> committed;
    std::map<int, bool> active;

    // PASS 1: Analysis Pass
    for (const auto& entry : logs) {
        if (entry.type == LOG_START) {
            active[entry.txnId] = true;
            committed[entry.txnId] = false;
        } else if (entry.type == LOG_COMMIT) {
            committed[entry.txnId] = true;
        } else if (entry.type == LOG_ABORT) {
            committed[entry.txnId] = false;
        }
    }

    std::vector<int> redoList;
    std::vector<int> undoList;

    for (const auto& pair : active) {
        int tid = pair.first;
        if (committed[tid]) {
            redoList.push_back(tid);
        } else {
            undoList.push_back(tid);
        }
    }

    std::cout << "[Analysis Pass Complete]\n";
    std::cout << " - Transactions to REDO (Committed): ";
    for (int tid : redoList) std::cout << "#" << tid << " ";
    if (redoList.empty()) std::cout << "None";
    std::cout << "\n - Transactions to UNDO (Uncommitted/In-Flight): ";
    for (int tid : undoList) std::cout << "#" << tid << " ";
    if (undoList.empty()) std::cout << "None";
    std::cout << "\n-------------------------------------------------------------------------\n";

    // PASS 2: REDO PASS (Ensure Durability)
    for (const auto& entry : logs) {
        if (entry.type == LOG_UPDATE && committed[entry.txnId]) {
            if (entry.item.find("SEAT_") == 0) {
                int seatNum = std::stoi(entry.item.substr(5));
                seats[seatNum - 1].isBooked = true;
                seats[seatNum - 1].passengerName = entry.newValue;
                std::cout << "[REDO Phase] Re-applied " << entry.item << " -> " << entry.newValue << " (Txn #" << entry.txnId << ")\n";
            }
        }
    }

    // PASS 3: UNDO PASS (Ensure Atomicity - rolled back in reverse order)
    for (auto it = logs.rbegin(); it != logs.rend(); ++it) {
        if (it->type == LOG_UPDATE && !committed[it->txnId]) {
            if (it->item.find("SEAT_") == 0) {
                int seatNum = std::stoi(it->item.substr(5));
                seats[seatNum - 1].isBooked = false;
                seats[seatNum - 1].passengerName = "AVAILABLE";
                seats[seatNum - 1].bookedByUserId = -1;
                std::cout << "[UNDO Phase] Rolled back " << it->item << " -> Restored to: " << it->oldValue << " (Txn #" << it->txnId << ")\n";
            }
        }
    }

    std::cout << "================ [BIJENDRA DBMS] RECOVERY COMPLETED SUCCESSFULLY ==========\n\n";
}

void EndureAirDatabase::printSeatMap() {
    std::cout << "\n================ [BIJENDRA DBMS] LIVE CABIN SEAT MAP ================\n";
    for (int i = 0; i < DB_TOTAL_SEATS; ++i) {
        std::cout << "Seat " << std::setw(2) << seats[i].seatNumber << " : ";
        if (seats[i].isBooked) {
            std::cout << "[OCCUPIED] Passenger: " << std::left << std::setw(15) << seats[i].passengerName 
                      << " (User ID: " << seats[i].bookedByUserId << ")\n";
        } else {
            std::cout << "[AVAILABLE]\n";
        }
    }
    std::cout << "---------------------------------------------------------------------\n";
    std::cout << "Inventory: Meals Remaining = " << availableMeals 
              << " | Baggage Available = " << availableBaggageKg << " kg\n";
    std::cout << "=====================================================================\n\n";
}

void EndureAirDatabase::printAuditReport() {
    std::cout << "\n================ [BIJENDRA DBMS] SQL TRANSACTION AUDIT REPORT ================\n";
    if (bookingsTable.empty()) {
        std::cout << ">> No committed bookings recorded in table yet.\n";
    } else {
        std::cout << std::left << std::setw(8) << "BookID"
                  << std::setw(8) << "TxnID"
                  << std::setw(8) << "UserID"
                  << std::setw(16) << "Passenger"
                  << std::setw(8) << "Seat"
                  << std::setw(8) << "Meals"
                  << std::setw(10) << "Baggage"
                  << std::setw(12) << "Status\n";
        std::cout << "-----------------------------------------------------------------------------\n";
        for (const auto& b : bookingsTable) {
            std::cout << std::left << std::setw(8) << b.bookingId
                      << std::setw(8) << b.txnId
                      << std::setw(8) << b.userId
                      << std::setw(16) << b.passengerName
                      << std::setw(8) << b.seatNumber
                      << std::setw(8) << b.meals
                      << std::setw(10) << (std::to_string(b.baggageKg) + "kg")
                      << std::setw(12) << b.status << "\n";
        }
    }
    std::cout << "=============================================================================\n\n";
}

bool EndureAirDatabase::isSeatBooked(int seatNum) {
    if (seatNum < 1 || seatNum > DB_TOTAL_SEATS) return true;
    return seats[seatNum - 1].isBooked;
}
