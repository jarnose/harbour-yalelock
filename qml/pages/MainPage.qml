import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    function showToast(message) {
        toastLabel.text = message
        toastAnimation.restart()
    }

    Connections {
        target: AppController
        onErrorOccurred: page.showToast(message)
    }

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: AppController.lockModel

        header: Column {
            width: parent.width

            PageHeader {
                title: qsTr("Yale Lock")
            }

            ViewPlaceholder {
                enabled: listView.count === 0
                text: qsTr("No locks added yet")
                hintText: qsTr("Pull down to add your lock's Bluetooth address and offline key")
            }
        }

        PullDownMenu {
            MenuItem {
                text: qsTr("Add lock")
                onClicked: pageStack.push(Qt.resolvedUrl("AddLockPage.qml"))
            }
        }

        delegate: ListItem {
            id: delegateItem
            contentHeight: Theme.itemSizeLarge

            ListView.onRemove: animateRemoval(delegateItem)

            function requestDelete() {
                var idToDelete = model.id
                remorseAction(qsTr("Removing"), function() {
                    AppController.removeLock(idToDelete)
                })
            }

            menu: Component {
                ContextMenu {
                    MenuItem {
                        text: qsTr("Rename")
                        onClicked: pageStack.push(Qt.resolvedUrl("RenameLockPage.qml"), {
                            "lockId": model.id,
                            "name": model.name
                        })
                    }
                    MenuItem {
                        text: model.connectionState === "connected" ? qsTr("Disconnect") : qsTr("Connect")
                        onClicked: model.connectionState === "connected"
                                   ? AppController.disconnectLock(model.id)
                                   : AppController.connectLock(model.id)
                    }
                    MenuItem {
                        text: qsTr("Refresh status")
                        onClicked: AppController.refreshStatus(model.id)
                    }
                    MenuItem {
                        text: qsTr("Remove")
                        onClicked: delegateItem.requestDelete()
                    }
                }
            }

            Row {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                }
                spacing: Theme.paddingMedium

                Column {
                    width: parent.width - unlockButton.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter

                    Label {
                        text: model.name
                        color: delegateItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        truncationMode: TruncationMode.Fade
                    }
                    Label {
                        text: {
                            if (model.lastError)
                                return model.lastError
                            if (model.connectionState === "connecting")
                                return qsTr("Connecting…")
                            if (model.connectionState === "error")
                                return qsTr("Connection error")
                            var parts = []
                            if (model.lockStatus)
                                parts.push(model.lockStatus)
                            if (model.doorStatus)
                                parts.push(qsTr("door %1").arg(model.doorStatus))
                            return parts.length > 0 ? parts.join(" · ") : qsTr("Not connected")
                        }
                        color: model.lastError || model.connectionState === "error"
                               ? Theme.errorColor
                               : (delegateItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor)
                        font.pixelSize: Theme.fontSizeExtraSmall
                        truncationMode: TruncationMode.Fade
                    }
                }

                Item {
                    id: unlockButton
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(button.width, busyIndicator.width)
                    height: Math.max(button.height, busyIndicator.height)

                    Button {
                        id: button
                        anchors.centerIn: parent
                        visible: !model.busy
                        text: model.lockStatus === "locked" ? qsTr("Unlock") : qsTr("Lock")
                        onClicked: model.lockStatus === "locked"
                                   ? AppController.unlockDoor(model.id)
                                   : AppController.lockDoor(model.id)
                    }

                    BusyIndicator {
                        id: busyIndicator
                        anchors.centerIn: parent
                        size: BusyIndicatorSize.Small
                        running: model.busy
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }

    Label {
        id: toastLabel
        anchors {
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge
            horizontalCenter: parent.horizontalCenter
        }
        opacity: 0
        color: Theme.highlightColor
        horizontalAlignment: Text.AlignHCenter

        SequentialAnimation {
            id: toastAnimation
            NumberAnimation { target: toastLabel; property: "opacity"; to: 1; duration: 150 }
            PauseAnimation { duration: 2500 }
            NumberAnimation { target: toastLabel; property: "opacity"; to: 0; duration: 300 }
        }
    }
}
