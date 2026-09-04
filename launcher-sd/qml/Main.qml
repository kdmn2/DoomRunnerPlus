//======================================================================================================================
// DoomRunnerSD - a minimal controller-first launcher (Qt Quick)
//
// Navigation is keyboard/gamepad-driven: Up/Down move the selection, Enter/A launches,
// Escape/B quits. On the Steam Deck, Steam Input maps the controller to these keys.
//======================================================================================================================

import QtQuick 2.15
import QtQuick.Window 2.15

Window {
	id: window
	width: 1280
	height: 800
	visible: true
	title: qsTr("Doom Runner Plus — Steam Deck Launcher")
	color: "#101217"

	property var presetList: backend.presets() || []
	property int selected: 0

	Item {
		id: root
		anchors.fill: parent
		focus: true

		Keys.onPressed: (event) => {
			switch (event.key) {
				case Qt.Key_Up:
				case Qt.Key_W:
					if (selected > 0) selected--
					event.accepted = true
					break
				case Qt.Key_Down:
				case Qt.Key_S:
					if (selected < presetList.length - 1) selected++
					event.accepted = true
					break
				case Qt.Key_Return:
				case Qt.Key_Enter:
					backend.launchPreset(selected)
					event.accepted = true
					break
				case Qt.Key_Escape:
				case Qt.Key_Q:
					Qt.quit()
					event.accepted = true
					break
			}
		}

		Column {
			anchors.fill: parent
			anchors.margins: 56
			spacing: 14

			Text {
				text: qsTr("Doom Runner Plus")
				color: "#ffffff"
				font.pixelSize: 44
				font.bold: true
			}
			Text {
				text: qsTr("Select a preset and press Enter / A to launch · Esc / B to quit")
				color: "#8a8f96"
				font.pixelSize: 20
			}

			Rectangle {
				width: parent.width
				height: 2
				color: "#2c3138"
			}

			ListView {
				id: list
				width: parent.width
				height: parent.height * 0.66
				model: presetList
				currentIndex: selected
				clip: true
				highlightMoveDuration: 0
				boundsBehavior: Flickable.StopAtBounds

				delegate: Rectangle {
					width: list.width
					height: 68
					color: list.currentIndex === index ? "#2a6bd6" : "#1b1e24"
					radius: 10
					Behavior on color { ColorAnimation { duration: 100 } }

					Text {
						anchors.left: parent.left
						anchors.leftMargin: 24
						anchors.verticalCenter: parent.verticalCenter
						text: modelData
						color: "#ffffff"
						font.pixelSize: 26
					}
				}
			}

			Rectangle {
				width: parent.width
				height: 2
				color: "#2c3138"
			}

			Text {
				id: cmdLabel
				width: parent.width
				wrapMode: Text.Wrap
				text: presetList.length ? "command: " + backend.commandFor(selected) : "config not found: " + (backend.configPath() || "—")
				color: "#6f747b"
				font.pixelSize: 15
			}
		}

		Connections {
			target: backend
			function onErrorOccurred(message) { cmdLabel.text = message }
		}
	}
}
