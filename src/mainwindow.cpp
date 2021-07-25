/*
 * LeanHRPT Decode
 * Copyright (C) 2021 Xerbo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QPushButton>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>
#include <QScrollBar>

#include "fingerprint.h"
#include "geometry.h"

#include "decoders/meteor.h"
#include "decoders/noaa.h"
#include "decoders/fengyun.h"
#include "decoders/metop.h"

template<typename T>
T clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(hi, v));
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), manager("presets.ini") {
    ui = new Ui::MainWindow;
    ui->setupUi(this);

    compositor = new ImageCompositor;
    graphicsScene = new QGraphicsScene;
    decodeWatcher = new QFutureWatcher<void>(this);
    QFutureWatcher<void>::connect(decodeWatcher, &QFutureWatcher<void>::finished, this, &MainWindow::decodeFinished);

    ui->channelView->setScene(graphicsScene);
    ui->compositeView->setScene(graphicsScene);
    ui->presetView->setScene(graphicsScene);

    zoomIn = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Plus), this);
    zoomOut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Minus), this);
    flip = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_F), this);

    QShortcut::connect(zoomIn, &QShortcut::activated, [this]() {
        incrementZoom(1);
    });
    QShortcut::connect(zoomOut, &QShortcut::activated, [this]() {
        incrementZoom(-1);
    });
    QShortcut::connect(flip, &QShortcut::activated, [this]() {
        ui->actionFlip->setChecked(!ui->actionFlip->isChecked());
        on_actionFlip_triggered();
    });

    setState(WindowState::Idle);
}

MainWindow::~MainWindow() {
    delete zoomIn;
    delete zoomOut;
    delete flip;
    delete compositor;
    delete graphicsScene;
    delete decodeWatcher;
    delete ui;
}

void MainWindow::incrementZoom(int amount) {
    int newIndex = clamp(ui->zoomSelector->currentIndex() + amount, 0, ui->zoomSelector->count()-1);
    ui->zoomSelector->setCurrentIndex(newIndex);
    on_zoomSelector_activated(newIndex);
}

void MainWindow::setState(WindowState state) {
    QWidget *items[] = {
        ui->groupBox,
        ui->menuOptions,
        ui->stackedOptions,
        ui->zoomSelectorBox
    };

    for (QWidget *item : items) {
        item->setEnabled(state == WindowState::Finished);
    }

    ui->actionSave_All_Channels->setEnabled(state == WindowState::Finished);
    ui->actionSave_Current_Image->setEnabled(state == WindowState::Finished);
    ui->actionSave_Current_Image_Corrected->setEnabled(state == WindowState::Finished);
    ui->actionFlip->setEnabled(state == WindowState::Finished);
}

void MainWindow::populateChannelSelectors(size_t channels) {
    QComboBox *comboBoxes[] = { ui->channelSelector, ui->redSelector, ui->greenSelector, ui->blueSelector };

    for (QComboBox *comboBox : comboBoxes) {
        comboBox->clear();
        for (size_t i = 0; i < channels; i++) {
            comboBox->addItem(QString("Channel %1").arg(i + 1));
        }
    }

    // RGB221
    ui->redSelector->setCurrentIndex(1);
    ui->greenSelector->setCurrentIndex(1);
    ui->blueSelector->setCurrentIndex(0);
    selectedComposite[0] = 2;
    selectedComposite[1] = 2;
    selectedComposite[2] = 1;
}

// Display a QImage on all QGraphicsView objects
void MainWindow::displayQImage(QImage *image) {
    graphicsScene->clear();
    graphicsScene->setSceneRect(0, 0, image->width(), image->height());
    graphicsScene->addPixmap(QPixmap::fromImage(*image));
}

void MainWindow::on_actionOpen_triggered() {
    QString filename = QFileDialog::getOpenFileName(this, "Open File", "", "Binary files (*.bin *.cadu)");

    if (!filename.isEmpty()) {
        Satellite satellite = fingerprint(filename.toStdString());

        // If the fingerprint can't detect the satellite then ask manually
        if (satellite == Unknown) {
            QMessageBox satelliteSelection;
            satelliteSelection.setWindowTitle("Select Satellite");
            satelliteSelection.setText("Which satellite did this file come from?");
            QPushButton *meteorButton = satelliteSelection.addButton("Meteor", QMessageBox::AcceptRole);
            QPushButton *noaaButton = satelliteSelection.addButton("NOAA", QMessageBox::AcceptRole);
            QPushButton *metopButton = satelliteSelection.addButton("MetOp", QMessageBox::AcceptRole);
            QPushButton *fengyunButton = satelliteSelection.addButton("FengYun", QMessageBox::AcceptRole);
            QPushButton *abortButton = satelliteSelection.addButton(QMessageBox::Abort);
            satelliteSelection.exec();

            if (satelliteSelection.clickedButton() == meteorButton) {
                satellite = Satellite::Meteor;
            } else if (satelliteSelection.clickedButton() == noaaButton) {
                satellite = Satellite::NOAA;
            } else if (satelliteSelection.clickedButton() == fengyunButton) {
                satellite = Satellite::FengYun;
            } else if (satelliteSelection.clickedButton() == metopButton) {
                satellite = Satellite::MetOp;
            } else if (satelliteSelection.clickedButton() == abortButton) {
                return;
            } else {
                throw std::runtime_error("invalid button pressed in satellite selector message box");
            }
        }

        setState(WindowState::Decoding);
        ui->statusbar->showMessage(QString("Decoding %1 (might take a while)...").arg(filename));
        decodeWatcher->setFuture(QtConcurrent::run(this, &MainWindow::startDecode, satellite, filename.toStdString()));
    }
}

void MainWindow::startDecode(Satellite satellite, std::string filename) {
    Decoder *decoder;
    switch (satellite) {
        case Satellite::FengYun: decoder = new FengyunDecoder; break;
        case Satellite::Meteor: decoder = new MeteorDecoder; break;
        case Satellite::MetOp: decoder = new MetOpDecoder; break;
        case Satellite::NOAA: decoder = new NOAADecoder; break;
        default: throw std::runtime_error("invalid value in enum `Satellite`");
    }
    imagerName = QString::fromStdString(decoder->imagerName());
    decoder->decodeFile(filename);
    sat = satellite;

    compositor->importFromRaw(decoder->getImage());

    delete decoder;
}

#include <iostream>
void MainWindow::decodeFinished() {
    if (compositor->height() == 0) {
        ui->statusbar->showMessage("Decode failed");
        setState(WindowState::Idle);
        return;
    }

    channel   = QImage(compositor->width(), compositor->height(), QImage::Format_Grayscale16);
    composite = QImage(compositor->width(), compositor->height(), QImage::Format_RGBX64);
    preset    = QImage(compositor->width(), compositor->height(), QImage::Format_RGBX64);

    populateChannelSelectors(compositor->channels());

    ui->statusbar->showMessage(QString("Decode finished: %1, %2 lines").arg(QString(imagerName)).arg(compositor->height()));

    selected_presets.clear();
    for (auto preset : manager.presets) {
        if (preset.second.satellites.count(sat)) {
            selected_presets.insert(preset);
        }
    }

    ui->presetSelector->clear();
    for (auto item : selected_presets) {
        ui->presetSelector->addItem(QString::fromStdString(item.first));
    }
    on_presetSelector_activated(ui->presetSelector->currentText());

    setEqualization(selectedEqualization);

    reloadImage();
    setState(WindowState::Finished);
}

// Zoom selector combo box
void MainWindow::on_zoomSelector_activated(int index) {
    float zoomLevels[] = { 0.25f, 0.5f, 1.0f, 2.0f };
    QGraphicsView *views[] = { ui->channelView, ui->compositeView, ui->presetView };

    for(QGraphicsView *view : views) {
        view->resetTransform();
        view->scale(zoomLevels[index], zoomLevels[index]);
    }
}

void MainWindow::setChannel(int sensor_channel) {
    selectedChannel = sensor_channel + 1;
    compositor->getChannel(&channel, selectedChannel);
    reloadImage();
}
void MainWindow::setCompositeChannel(int channel, int sensor_channel) {
    selectedComposite[channel] = sensor_channel + 1;
    compositor->getComposite(&composite, selectedComposite);
    reloadImage();
}

void MainWindow::setEqualization(Equalization type) {
    selectedEqualization = type;
    compositor->setEqualization(type);
    compositor->getComposite(&composite, selectedComposite);
    compositor->getChannel(&channel, selectedChannel);
    compositor->getExpression(&preset, selected_presets.at(ui->presetSelector->currentText().toStdString()).expression);
    reloadImage();
}

void MainWindow::on_actionFlip_triggered() {
    compositor->flip();
    compositor->getComposite(&composite, selectedComposite);
    compositor->getChannel(&channel, selectedChannel);
    compositor->getExpression(&preset, selected_presets.at(ui->presetSelector->currentText().toStdString()).expression);
    reloadImage();
}

void MainWindow::reloadImage() {
    on_imageTabs_currentChanged(ui->imageTabs->currentIndex());
}
void MainWindow::on_imageTabs_currentChanged(int index) {
    QGraphicsView *tabs[] = { ui->channelView, ui->compositeView, ui->presetView };
    tabs[index]->horizontalScrollBar()->setValue(tabs[previousTabIndex]->horizontalScrollBar()->value());
    tabs[index]->verticalScrollBar()->setValue(tabs[previousTabIndex]->verticalScrollBar()->value());

    switch (index) {
        case 0: displayQImage(&channel); break;
        case 1: displayQImage(&composite); break;
        case 2: displayQImage(&preset); break;
        default: throw std::runtime_error("invalid tab index");
    }

    previousTabIndex = index;
}

void MainWindow::saveCurrentImage(bool corrected) {
    QString types[3] = { QString::number(selectedChannel), "Composite", ui->presetSelector->currentText() };
    QString name = QString("%1-%2.png").arg(imagerName).arg(types[ui->imageTabs->currentIndex()]);
    QString filename = QFileDialog::getSaveFileName(this, "Save Current Image", name, "PNG (*.png);;JPEG (*.jpg *.jpeg);;WEBP (*.webp);; BMP (*.bmp)");

    QtConcurrent::run(this, &MainWindow::writeCurrentImage, filename, corrected);
}

void MainWindow::writeCurrentImage(QString filename, bool corrected) {
    if (corrected) {
        switch (ui->imageTabs->currentIndex()) {
            case 0: correct_geometry(channel, sat).save(filename); break;
            case 1: correct_geometry(composite, sat).save(filename); break;
            case 2: correct_geometry(preset, sat).save(filename); break;
            default: throw std::runtime_error("invalid tab index");
        }
    } else {
        switch (ui->imageTabs->currentIndex()) {
            case 0: channel.save(filename); break;
            case 1: composite.save(filename); break;
            case 2: preset.save(filename); break;
            default: throw std::runtime_error("invalid tab index");
        }
    }
}

void MainWindow::on_actionSave_All_Channels_triggered() {
    QString directory = QFileDialog::getExistingDirectory(this, "Save All Channels", "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!directory.isEmpty()) {
        QtConcurrent::run(this, &MainWindow::saveAllChannels, directory);
    }
}

void MainWindow::saveAllChannels(QString directory) {
    QImage channel(compositor->width(), compositor->height(), QImage::Format_Grayscale16);

    for(size_t i = 0; i < compositor->channels(); i++) {
        ui->statusbar->showMessage(QString("Saving channel %1...").arg(i + 1));
        compositor->getChannel(&channel, i + 1);
        channel.save(QString("%1/%2-%3.png").arg(directory).arg(imagerName).arg(i + 1), "PNG");
    }

    ui->statusbar->showMessage("Done");
}

void MainWindow::on_presetSelector_activated(QString text) {
    Preset preset2 = selected_presets.at(text.toStdString());
    ui->presetDescription->setText(QString::fromStdString(preset2.description));
    ui->presetAuthor->setText(QString::fromStdString(preset2.author));
    ui->presetCategory->setText(QString::fromStdString(preset2.category));
    compositor->getExpression(&preset, preset2.expression);
    reloadImage();
}
