import paho.mqtt.client as mqtt
from PIL import Image
import io
import os
from datetime import datetime
import dlib
import cv2
import numpy as np
import tkinter as tk
from tkinter import ttk

# MQTT Broker的URL
BROKER_URL = "broker.mqttgo.io"
PORT = 1883
TOPIC = "isu/class/face"
SAVE_DIR = "face_photo"
RESULT_TOPIC = "isu/class/number"

# 確保保存影像的目錄存在
os.makedirs(SAVE_DIR, exist_ok=True)
os.makedirs("no_face", exist_ok=True)
os.makedirs("different_face", exist_ok=True)

# 載入 dlib 的臉部偵測器和臉部特徵提取器
detector = dlib.get_frontal_face_detector()
predictor = dlib.shape_predictor("shape_predictor_68_face_landmarks.dat")
face_rec = dlib.face_recognition_model_v1("dlib_face_recognition_resnet_model_v1.dat")

students = ["10903069", "10903021", "10903017", "10903011", "10903088"]
statuses = {student: "未到" for student in students}

# 初始化Tkinter視窗
root = tk.Tk()
root.title("Face Recognition Status")
root.geometry("300x250")

status_labels = {}

# 建立並放置狀態標籤
for i, student in enumerate(students):
    label = ttk.Label(root, text=f"{student}: {statuses[student]}")
    label.grid(row=i, column=0, padx=10, pady=5, sticky=tk.W)
    status_labels[student] = label

def update_labels():
    for student, status in statuses.items():
        status_text = f"{student}: {status}" if status == "已簽到" else f"{student}: {status}"
        status_labels[student].config(text=status_text)
    root.update_idletasks()

def get_face_descriptor(image_path):
    image = cv2.imread(image_path)
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    faces = detector(gray)
    if len(faces) == 0:
        return None
    shape = predictor(gray, faces[0])
    face_descriptor = face_rec.compute_face_descriptor(image, shape)
    return np.array(face_descriptor)

def compare_faces(face_descriptor1, face_descriptor2, threshold=0.38):
    distance = np.linalg.norm(face_descriptor1 - face_descriptor2)
    return distance < threshold

def find_matching_folder(reference_descriptor, folders):
    for folder_path in folders:
        for filename in os.listdir(folder_path):
            file_path = os.path.join(folder_path, filename)
            if os.path.isfile(file_path):
                face_descriptor = get_face_descriptor(file_path)
                if face_descriptor is not None:
                    if compare_faces(reference_descriptor, face_descriptor):
                        return os.path.basename(folder_path)
    return None

def classify_images_with_multiple_folders(reference_image_path, folders, mqtt_client):
    reference_descriptor = get_face_descriptor(reference_image_path)
    if reference_descriptor is None:
        print("未偵測到任何臉部")
        # 将图片移动到no_face文件夹
        no_face_path = os.path.join("no_face", os.path.basename(reference_image_path))
        os.rename(reference_image_path, no_face_path)
        mqtt_client.publish(RESULT_TOPIC, "none")
        return

    matching_folder = find_matching_folder(reference_descriptor, folders)
    if matching_folder is not None:
        print(f"找到相同人臉的資料夾名稱: {matching_folder}")
        # 將圖片移動到匹配的資料夾中
        matched_folder_path = os.path.join(matching_folder)  # 修改這裡
        os.makedirs(matched_folder_path, exist_ok=True)
        matched_image_path = os.path.join(matched_folder_path, os.path.basename(reference_image_path))
        os.rename(reference_image_path, matched_image_path)
        # 發布符合的資料夾名稱到MQTT主題
        mqtt_client.publish(RESULT_TOPIC, matching_folder)
        statuses[matching_folder] = "已簽到"
    else:
        print("未找到相同人臉的資料夾")
        # 將圖片移動到different_face資料夾中
        no_same_face_path = os.path.join("different_face", os.path.basename(reference_image_path))
        os.rename(reference_image_path, no_same_face_path)
        mqtt_client.publish(RESULT_TOPIC, "none")
    update_labels()

# 定義接收到訊息時的回調函數
def on_message(client, userdata, message):
    try:
        # 從訊息中讀取影像數據
        img_data = message.payload

        # 將影像資料轉換為PIL影像
        image = Image.open(io.BytesIO(img_data))

        # 產生唯一的檔名
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        file_name = f"{timestamp}.jpg"
        file_path = os.path.join(SAVE_DIR, file_name)

        # 保存影像
        image.save(file_path)
        print(f"Image saved as {file_path}")

        # 使用已儲存的影像進行人臉辨識並分類
        reference_image_path = file_path
        folders = [os.path.join(student) for student in students]  # 修改這裡
        classify_images_with_multiple_folders(reference_image_path, folders, client)
    except Exception as e:
        print(f"Error processing image: {e}")

# 定義連接到MQTT Broker時的回調函數
def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    # 訂閱指定主題
    client.subscribe(TOPIC)

# MQTT客戶端
client = mqtt.Client()

# 設定回調函數
client.on_connect = on_connect
client.on_message = on_message

# 連接到MQTT Broker
client.connect(BROKER_URL, PORT, 60)

# 開始監聽MQTT
client.loop_start()

# 定義結束簽到的函數
def end_sign_in():
    client.loop_stop()
    root.quit()

# 結束簽到按鈕
end_button = ttk.Button(root, text="結束簽到", command=end_sign_in)
end_button.grid(row=len(students), column=0, pady=10)

# 啟動Tkinter主循環
root.mainloop()