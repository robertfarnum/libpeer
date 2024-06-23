/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

'use strict';

const startButton = document.getElementById('startButton');
const stopButton = document.getElementById('stopButton');
const callButton = document.getElementById('callButton');
const hangupButton = document.getElementById('hangupButton');

hangupButton.disabled = true;

const video = document.getElementById('video');

let pc = null;
let localStream = null;

const uuid = "c64141a8-d994-11ee-b858-00155ddd17eb";
const serverTopic = 'webrtc/' + uuid + '/server';
const clientTopic = 'webrtc/' + uuid + '/client';
const signaling = mqtt.connect('wss://mqtt.eclipseprojects.io/mqtt');
let publishTopic = null;

signaling.on('connect', async function () {
});

signaling.on('message', async function (topic, message) {
    const data = JSON.parse(message)
    switch (data.type) {
        case 'offer':
            handleOffer(data);
            break;
        case 'answer':
            handleAnswer(data);
            break;
        case 'candidate':
            handleCandidate(data);
            break;
        case 'call':
            handleCall();
            break;
        case 'hangup':
            handleHangup();
            break;
        default:
            console.log('unhandled', message);
            break;
    }
});

startButton.onclick = async () => {
    publishTopic = clientTopic;

    try {
        localStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: true });
        video.srcObject = localStream;

        if (signaling) {
            signaling.subscribe(serverTopic, function (err) {});
        }

        startButton.disabled = true;
        stopButton.disabled = false;
        callButton.disabled = true;
        hangupButton.disabled = false;
    } catch(e) {
        console.log(e);
    }
};

stopButton.onclick = async () => {
    hangup();
};

callButton.onclick = async () => {
    publishTopic = serverTopic;

    if (signaling) {
        signaling.subscribe(clientTopic, function (err) {});
    }

    makeCall();

    startButton.disabled = true;
    stopButton.disabled = true;
    callButton.disabled = true;
    hangupButton.disabled = false;
};

hangupButton.onclick = async () => {
    const message = JSON.stringify({type:'hangup'});
    signaling.publish(publishTopic, message);

    hangup();
};

async function handleHangup() {
    hangup();
}

async function hangup() {
    if (pc) {
        pc.close();
        pc = null;
    }

    if (localStream) {
        localStream.getTracks().forEach(track => track.stop());
        localStream = null;
    }

    if (signaling) {
        signaling.unsubscribe(clientTopic, function (err) {});
        signaling.unsubscribe(serverTopic, function (err) {});
    }

    startButton.disabled = false;
    stopButton.disabled = true;
    callButton.disabled = false;
    hangupButton.disabled = true;
}

function createPeerConnection() {
    pc = new RTCPeerConnection();
    pc.onicecandidate = e => {
        const candidate = {
            type: 'candidate',
            candidate: null,
        };
        if (e.candidate) {
            candidate.candidate = e.candidate.candidate;
            candidate.sdpMid = e.candidate.sdpMid;
            candidate.sdpMLineIndex = e.candidate.sdpMLineIndex;
        }
        const message = JSON.stringify(candidate);
        signaling.publish(publishTopic, message);
    };
    pc.ontrack = e => video.srcObject = e.streams[0];
    if (localStream) {
        localStream.getTracks().forEach(track => pc.addTrack(track, localStream));
    }
}

async function makeCall() {
    const message = JSON.stringify({type:'call'});
    signaling.publish(publishTopic, message);
}

async function handleCall() {
    // A second tab joined. This tab will initiate a call unless in a call already.
    if (pc) {
        console.log('already in call, ignoring');
        return;
    }

    createPeerConnection();

    const offer = await pc.createOffer();
    signaling.publish(publishTopic, JSON.stringify({ type: 'offer', sdp: offer.sdp }));
    await pc.setLocalDescription(offer);
}

async function handleOffer(offer) {
    if (pc) {
        console.error('existing peer connection');
        return;
    }

    createPeerConnection();
    await pc.setRemoteDescription(offer);

    const answer = await pc.createAnswer();
    signaling.publish(publishTopic, JSON.stringify({ type: 'answer', sdp: answer.sdp }));
    await pc.setLocalDescription(answer);
}

async function handleAnswer(answer) {
    if (!pc) {
        console.error('no peer connection');
        return;
    }

    if (!localStream) {
        return;
    }

    await pc.setRemoteDescription(answer);
}

async function handleCandidate(candidate) {
    if (!pc) {
        console.error('no peer connection');
        return;
    }
    if (!candidate.candidate) {
        await pc.addIceCandidate(null);
    } else {
        await pc.addIceCandidate(candidate);
    }
}
