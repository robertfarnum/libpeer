/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

'use strict';

const answerButton = document.getElementById('answerButton');
const callButton = document.getElementById('callButton');
const hangupButton = document.getElementById('hangupButton');

hangupButton.disabled = true;


let pc = null;
let localStream = null;

const uuid = "c64141a8-d994-11ee-b858-00155ddd17eb";
const callTopic = 'webrtc/' + uuid + '/call';
const serverTopic = 'webrtc/' + uuid + '/server';
const clientTopic = 'webrtc/' + uuid + '/client';
const signaling = mqtt.connect('wss://mqtt.eclipseprojects.io/mqtt');

let publishTopic = null;

signaling.on('connect', async function () {
    console.log('signalling connected')

    if (signaling) {
        signaling.subscribe(callTopic, function (err) {});
    }
});

signaling.on('message', async function (topic, message) {
    const data = JSON.parse(message)
    switch (data.type) {
        case 'call':
            handleCall(data);
            break;
        case 'offer':
            handleOffer(data);
            break;
        case 'answer':
            handleAnswer(data);
            break;
        case 'candidate':
            handleCandidate(data);
            break;
        case 'hangup':
            handleHangup();
            break;
        default:
            console.log('unhandled', data);
            break;
    }
});

answerButton.onclick = async () => {
    publishTopic = clientTopic;

    if (signaling) {
        signaling.subscribe(serverTopic, function (err) {});
    }

    answerButton.disabled = true;
    callButton.disabled = true;
    hangupButton.disabled = false;

    startVideo();
};

callButton.onclick = async () => {
    publishTopic = serverTopic;

    callButton.disabled = true;
    answerButton.disabled = true;
    hangupButton.disabled = false;

    if (signaling) {
        signaling.subscribe(clientTopic, function (err) {});
    }

    const message = JSON.stringify({type:'call'});
    signaling.publish(callTopic, message);
};

async function hangup() {
    answerButton.disabled = true;
    callButton.disabled = false;
    hangupButton.disabled = true;

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

}

hangupButton.onclick = async () => {
    hangup();

    const message = JSON.stringify({type:'hangup'});
    signaling.publish(callTopic, message);
};

async function handleHangup() {
    hangup();
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
    }
    pc.onnegotiationneeded = e => {

    }

    pc.ontrack = e => {
        video.srcObject = e.streams[0];
    }

    if (localStream) {
        localStream.getTracks().forEach(track => pc.addTrack(track, localStream));
    }
}

async function handleCall(call) {
    if(callButton.disabled) {
        return;
    }

     if (pc) {
        console.log('already in call, ignoring');
        return;
    }

    callButton.disabled = true;
    answerButton.disabled = false;
    hangupButton.disabled = false;
}

async function startVideo() {
    try {
        localStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: true });
        let video = document.getElementById('video');
        video.srcObject = localStream;

        createPeerConnection();

        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        signaling.publish(publishTopic, JSON.stringify({ type: 'offer', sdp: offer.sdp }));
    } catch(e) {
        console.log(e);
    }
}

async function handleOffer(offer) {
    if (pc) {
        console.error('existing peer connection');
        return;
    }

    createPeerConnection();
    await pc.setRemoteDescription(offer);

    const answer = await pc.createAnswer();
    await pc.setLocalDescription(answer);
    signaling.publish(publishTopic, JSON.stringify({ type: 'answer', sdp: answer.sdp }));
}

async function handleAnswer(answer) {
    if (!pc) {
        console.error('no peer connection');
        return;
    }

    if (!localStream) {
        console.log('no video available');
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
