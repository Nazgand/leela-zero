/*
    This file is part of Leela Zero.
    Copyright (C) 2017-2018 Marco Calignano
    Copyright (C) 2018 SAI Team

    Leela Zero is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Leela Zero is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Leela Zero.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cmath>
#include <QUuid>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include "Game.h"

Game::Game(const QString& weights, const QString& opt, const QString& binary) :
    QProcess(),
    m_cmdLine(""),
    m_binary(binary),
    m_timeSettings("time_settings 0 1 0"),
    m_scoreEarly(false),
    m_resignation(false),
    m_blackToMove(true),
    m_blackResigned(false),
    m_passes(0),
    m_moveNum(0)
{
#ifdef WIN32
    m_binary.append(".exe");
#endif
    m_cmdLine = m_binary + " " + opt + " " + weights;
    m_fileName = QUuid::createUuid().toRfc4122().toHex();
}

bool Game::checkGameEnd() {
    return (m_resignation ||
            m_passes > 1 ||
            m_moveNum > (BOARD_SIZE * BOARD_SIZE * 2));
}

void Game::error(int errnum) {
    QTextStream(stdout) << "*ERROR*: ";
    switch (errnum) {
        case Game::NO_LEELAZ:
            QTextStream(stdout)
                << "No 'leelaz' binary found." << endl;
            break;
        case Game::PROCESS_DIED:
            QTextStream(stdout)
                << "The 'leelaz' process died unexpected." << endl;
            break;
        case Game::WRONG_GTP:
            QTextStream(stdout)
                << "Error in GTP response." << endl;
            break;
        case Game::LAUNCH_FAILURE:
            QTextStream(stdout)
                << "Could not talk to engine after launching." << endl;
            break;
        default:
            QTextStream(stdout)
                << "Unexpected error." << endl;
            break;
    }
}

bool Game::eatNewLine() {
    char readBuffer[256];
    // Eat double newline from GTP protocol
    if (!waitReady()) {
        error(Game::PROCESS_DIED);
        return false;
    }
    auto readCount = readLine(readBuffer, 256);
    if (readCount < 0) {
        error(Game::WRONG_GTP);
        return false;
    }
    return true;
}

bool Game::sendGtpCommand(QString cmd) {
    QString response = sendGtpCommandForResponse(cmd);
    if (response.startsWith("= ")) {
        return true;
    }
    return false;
}

QString Game::sendGtpCommandForResponseTrimmed(QString cmd) {
    return sendGtpCommandForResponse(cmd).remove(0,2).trimmed();
}

QString Game::sendGtpCommandForResponse(QString cmd) {
    write(qPrintable(cmd.append("\n")));
    waitForBytesWritten(-1);
    if (!waitReady()) {
        error(Game::PROCESS_DIED);
        return "PROCESS_DIED";
    }
    char readBuffer[256];
    int readCount = readLine(readBuffer, 256);
    if (readCount <= 0 || readBuffer[0] != '=') {
        QTextStream(stdout) << "GTP: " << readBuffer << endl;
        error(Game::WRONG_GTP);
    }
    if (!eatNewLine()) {
        error(Game::PROCESS_DIED);
    }
    return readBuffer;
}

void Game::checkVersion(const VersionTuple &min_version) {
    write(qPrintable("version\n"));
    waitForBytesWritten(-1);
    if (!waitReady()) {
        error(Game::LAUNCH_FAILURE);
        exit(EXIT_FAILURE);
    }
    char readBuffer[256];
    int readCount = readLine(readBuffer, 256);
    //If it is a GTP comment just print it and wait for the real answer
    //this happens with the winogard tuning
    if (readBuffer[0] == '#') {
        readBuffer[readCount-1] = 0;
        QTextStream(stdout) << readBuffer << endl;
        if (!waitReady()) {
            error(Game::PROCESS_DIED);
            exit(EXIT_FAILURE);
        }
        readCount = readLine(readBuffer, 256);
    }
    // We expect to read at last "=, space, something"
    if (readCount <= 3 || readBuffer[0] != '=') {
        QTextStream(stdout) << "GTP: " << readBuffer << endl;
        error(Game::WRONG_GTP);
        exit(EXIT_FAILURE);
    }
    QString version_buff(&readBuffer[2]);
    version_buff = version_buff.simplified();
    QStringList version_list = version_buff.split(".");
    if (version_list.size() < 2) {
        QTextStream(stdout)
            << "Unexpected Leela Zero version: " << version_buff << endl;
        exit(EXIT_FAILURE);
    }
    if (version_list.size() < 3) {
        version_list.append("0");
    }
    int versionCount = (version_list[0].toInt() - std::get<0>(min_version)) * 10000;
    versionCount += (version_list[1].toInt() - std::get<1>(min_version)) * 100;
    versionCount += version_list[2].toInt() - std::get<2>(min_version);
    if (versionCount < 0) {
        QTextStream(stdout)
            << "Leela version is too old, saw " << version_buff
            << " but expected "
            << std::get<0>(min_version) << "."
            << std::get<1>(min_version) << "."
            << std::get<2>(min_version)  << endl;
        QTextStream(stdout)
            << "Check https://github.com/gcp/leela-zero for updates." << endl;
        exit(EXIT_FAILURE);
    }
    if (!eatNewLine()) {
        error(Game::WRONG_GTP);
        exit(EXIT_FAILURE);
    }
}

bool Game::gameStart(const VersionTuple &min_version) {
    start(m_cmdLine);
    if (!waitForStarted()) {
        error(Game::NO_LEELAZ);
        return false;
    }
    // This either succeeds or we exit immediately, so no need to
    // check any return values.
    checkVersion(min_version);
    QTextStream(stdout) << "Engine has started." << endl;
    sendGtpCommand(m_timeSettings);
    QTextStream(stdout) << "Infinite thinking time set." << endl;
    return true;
}

void Game::move() {
    m_moveNum++;
    QString moveCmd;
    if (m_blackToMove) {
        moveCmd = "genmove b\n";
    } else {
        moveCmd = "genmove w\n";
    }
    write(qPrintable(moveCmd));
    waitForBytesWritten(-1);
}

void Game::setMovesCount(int moves) {
    m_moveNum = moves;
    m_blackToMove = (moves % 2) == 0;
}

bool Game::waitReady() {
    while (!canReadLine() && state() == QProcess::Running) {
        waitForReadyRead(-1);
    }
    // somebody crashed
    if (state() != QProcess::Running) {
        return false;
    }
    return true;
}

bool Game::readMove() {
    char readBuffer[256];
    int readCount = readLine(readBuffer, 256);
    if (readCount <= 3 || readBuffer[0] != '=') {
        error(Game::WRONG_GTP);
        QTextStream(stdout) << "Error read " << readCount << " '";
        QTextStream(stdout) << readBuffer << "'" << endl;
        terminate();
        return false;
    }
    // Skip "= "
    m_moveDone = readBuffer;
    m_moveDone.remove(0, 2);
    m_moveDone = m_moveDone.simplified();
    if (!eatNewLine()) {
        error(Game::PROCESS_DIED);
        return false;
    }
    QTextStream(stdout) << m_moveNum << " (";
    QTextStream(stdout) << (m_blackToMove ? "B " : "W ") << m_moveDone << ") ";
    QTextStream(stdout).flush();
    if (m_moveDone.compare(QStringLiteral("pass"),
                          Qt::CaseInsensitive) == 0) {
        m_passes++;
    } else if (m_moveDone.compare(QStringLiteral("resign"),
                                 Qt::CaseInsensitive) == 0) {
        m_resignation = true;
        m_blackResigned = m_blackToMove;
    } else {
        m_passes = 0;
    }
    return true;
}

bool Game::setMove(const QString& m) {
    if (!sendGtpCommand(m)) {
        return false;
    }
    m_moveNum++;
    QStringList moves = m.split(" ");
    if (moves.at(2)
        .compare(QStringLiteral("pass"), Qt::CaseInsensitive) == 0) {
        m_passes++;
    } else if (moves.at(2)
               .compare(QStringLiteral("resign"), Qt::CaseInsensitive) == 0) {
        m_resignation = true;
        m_blackResigned = (moves.at(1).compare(QStringLiteral("black"), Qt::CaseInsensitive) == 0);
    } else {
        m_passes = 0;
    }
    m_blackToMove = !m_blackToMove;
    return true;
}

bool Game::nextMove() {
    if (checkGameEnd()) {
        return false;
    }
    m_blackToMove = !m_blackToMove;
    return true;
}

bool Game::getScore(bool scoreEarly) {
    if (m_resignation) {
        if (m_blackResigned) {
            m_winner = QString(QStringLiteral("white"));
            m_result = "W+Resign : 0.000";
            QTextStream(stdout) << "Score: " << m_result << endl;
        } else {
            m_winner = QString(QStringLiteral("black"));
            m_result = "B+Resign : 1.000";
            QTextStream(stdout) << "Score: " << m_result << endl;
        }
    } else if (scoreEarly) {
        m_scoreEarly = true;
        m_winner = QString(QStringLiteral("early"));
        const float mean = getScoreEstimateMean();
        const float stdDev = getScoreEstimateStandardDeviation();
        const float piOverSqrt3 = 1.8137993642342178505940782576421557322840662480927405755698849353881231811;
        const float winRate = 1.0f/(1.0f+std::exp(-mean*piOverSqrt3/stdDev));
        m_result = "0";
        if (mean != 0.0) {
            m_result = (mean > 0 ? "B+" : "W+") + QString().setNum(abs(mean), 'f', 3);
        }
        m_result += " : " +  QString().setNum(winRate, 'f', 3);
    } else {
        m_result = sendGtpCommandForResponseTrimmed("final_score");
        if (m_result[0] == 'W') {
            m_winner = QString(QStringLiteral("white"));
            m_result += " : 0.000";
        } else if (m_result[0] == 'B') {
            m_winner = QString(QStringLiteral("black"));
            m_result += " : 1.000";
        } else if (m_result[0] == '0') {
            m_winner = QString(QStringLiteral("panda"));
            m_result += " : 0.500";
        }
        QTextStream(stdout) << "Score: " << m_result;
    }
    if (m_winner.isNull()) {
        QTextStream(stdout) << "No winner found" << endl;
        return false;
    }
    QTextStream(stdout) << "Winner: " << m_winner << endl;
    return true;
}

float Game::getScoreEstimateMean() {
    bool parse;
    float mean = sendGtpCommandForResponseTrimmed("estimate_score_mean").toFloat(&parse);
    if (!parse) {
        // a parsing error implies (mean == 0.0)
        error(Game::WRONG_GTP);
    }
    return mean;
}

float Game::getScoreEstimateStandardDeviation() {
    bool parse;
    float standard = sendGtpCommandForResponseTrimmed("estimate_score_standard_deviation").toFloat(&parse);
    if (!parse) {
        // a parsing error implies (standard == 0.0)
        error(Game::WRONG_GTP);
    }
    return standard;
}

int Game::getWinner() {
    if (m_winner.compare(QStringLiteral("white"), Qt::CaseInsensitive) == 0)
        return Game::WHITE;
    else if (m_winner.compare(QStringLiteral("black"), Qt::CaseInsensitive) == 0)
        return Game::BLACK;
    else if (m_winner.compare(QStringLiteral("panda"), Qt::CaseInsensitive) == 0)
        return Game::PANDA;
    else
        return Game::EARLY;
}

bool Game::writeSgf() {
    return sendGtpCommand(qPrintable("printsgf " + m_fileName + ".sgf"));
}

bool Game::loadTraining(const QString &fileName) {
    QTextStream(stdout) << "Loading " << fileName + ".train" << endl;
    return sendGtpCommand(qPrintable("load_training " + fileName + ".train"));

}

bool Game::saveTraining() {
     QTextStream(stdout) << "Saving " << m_fileName + ".train" << endl;
     return sendGtpCommand(qPrintable("save_training " + m_fileName + ".train"));
}


bool Game::loadSgf(const QString &fileName) {
    QTextStream(stdout) << "Loading " << fileName + ".sgf" << endl;
    return sendGtpCommand(qPrintable("loadsgf " + fileName + ".sgf"));
}

bool Game::loadSgf(const QString &fileName, int moves) {
    QTextStream(stdout) << "Loading " << fileName + ".sgf with " << moves << " moves"  << endl;
    return sendGtpCommand(qPrintable("loadsgf " + fileName + ".sgf " + QString::number(moves)));
}

bool Game::komi(float komi) {
    QTextStream(stdout) << "Setting komi " << komi << endl;
    return sendGtpCommand(qPrintable("komi " + QString::number(komi)));
}

bool Game::fixSgf(QString& weightFile, bool resignation) {
    QFile sgfFile(m_fileName + ".sgf");
    if (!sgfFile.open(QIODevice::Text | QIODevice::ReadOnly)) {
        return false;
    }
    QString sgfData = sgfFile.readAll();
    QRegularExpression re("PW\\[Human\\]");
    QString playerName("PB[Leela Zero ");
    QRegularExpression le("PB\\[Leela Zero \\S+ ");
    QRegularExpressionMatch match = le.match(sgfData);
    if (match.hasMatch()) {
        playerName = match.captured(0);
    }
    playerName = "PW" + playerName.remove(0, 2);
    playerName += weightFile.left(8);
    playerName += "]";
    sgfData.replace(re, playerName);

    if (resignation) {
        QRegularExpression oldResult("RE\\[B\\+.*\\]");
        QString newResult("RE[B+Resign] ");
        sgfData.replace(oldResult, newResult);
        if (!sgfData.contains(newResult, Qt::CaseInsensitive)) {
            QRegularExpression oldwResult("RE\\[W\\+.*\\]");
            sgfData.replace(oldwResult, newResult);
        }
        QRegularExpression lastpass(";W\\[tt\\]\\)");
        QString noPass(")");
        sgfData.replace(lastpass, noPass);
    } else if (m_scoreEarly) {
        const float mean = getScoreEstimateMean();
        QRegularExpression oldResult("RE\\[.*\\]");
        QString newResult("RE[0] ");
        if (mean != 0.0) {
            newResult = "RE[";
            newResult += (mean > 0 ? "B+" : "W+") + QString().setNum(abs(mean), 'f', 3) + "] ";
        }
        sgfData.replace(oldResult, newResult);
    }

    sgfFile.close();
    if (sgfFile.open(QFile::WriteOnly | QFile::Truncate)) {
        QTextStream out(&sgfFile);
        out << sgfData;
    }
    sgfFile.close();

    return true;
}

bool Game::dumpTraining() {
    return sendGtpCommand(
        qPrintable("dump_training " + m_winner + " " + m_fileName + ".txt"));
}

bool Game::dumpDebug() {
    return sendGtpCommand(
        qPrintable("dump_debug " + m_fileName + ".debug.txt"));
}

void Game::gameQuit() {
    write(qPrintable("quit\n"));
    waitForFinished(-1);
}
