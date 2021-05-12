import paho.mqtt.client as paho

import matplotlib.pyplot as plt

import time

import numpy as np


import serial


# https://os.mbed.com/teams/mqtt/wiki/Using-MQTT#python-client


# MQTT broker hosted on local machine

mqttc = paho.Client()


# Settings for connection

# TODO: revise host to your IP

host = "192.168.226.124"

topic = "Mbed"
gesture_index = []
messagelist = []
num = 0
serdev = '/dev/ttyACM0'
s = serial.Serial(serdev, 9600)
seq_num = []

# Callbacks

def on_connect(self, mosq, obj, rc):

    print("Connected rc: " + str(rc))


def on_message(mosq, obj, msg):
    global num
    print("[Received] Topic: " + msg.topic + ", Message: " + str((msg.payload).decode('UTF-8')) + "\n")
    num = num+1
    messagelist.append(str(msg.payload.decode('UTF-8')))
    gesture_index.append(messagelist[num-1][16])
    if num == 5 :
        x1 = np.array([1, 2, 3, 4, 5])
        y1 = np.array(gesture_index)
        print(gesture_index)
        s.write(bytes("/close1/run\r", 'UTF-8'))
        fig, ax = plt.subplots(2, 1)
        ax[0].plot(x1, y1)
        plt.show()
        
    
    
    
        
    

def on_subscribe(mosq, obj, mid, granted_qos):

    print("Subscribed OK")


def on_unsubscribe(mosq, obj, mid, granted_qos):

    print("Unsubscribed OK")


# Set callbacks

mqttc.on_message = on_message

mqttc.on_connect = on_connect

mqttc.on_subscribe = on_subscribe

mqttc.on_unsubscribe = on_unsubscribe


# Connect and subscribe

print("Connecting to " + host + "/" + topic)

mqttc.connect(host, port=1883, keepalive=60)

mqttc.subscribe(topic, 0)


# Publish messages from Python

  
    


# Loop forever, receiving messages

mqttc.loop_forever()