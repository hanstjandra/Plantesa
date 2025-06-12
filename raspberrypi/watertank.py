import os
import pyrebase
from dotenv import load_dotenv
from datetime import datetime

# Load environment variables from .env file
load_dotenv()

# Your Firebase Web Config from environment variables
config = {
    "apiKey": os.getenv("FIREBASE_API_KEY"),
    "authDomain": os.getenv("FIREBASE_AUTH_DOMAIN"),
    "projectId": os.getenv("FIREBASE_PROJECT_ID"),
    "storageBucket": os.getenv("FIREBASE_STORAGE_BUCKET"),
    "messagingSenderId": os.getenv("FIREBASE_MESSAGING_SENDER_ID"),
    "appId": os.getenv("FIREBASE_APP_ID"),
    "databaseURL": os.getenv("FIREBASE_DATABASE_URL", "")
}

# Initialize Firebase
firebase = pyrebase.initialize_app(config)
storage = firebase.storage()
db = firebase.database()

# Get the latest entry under SensorsData
latest_entry = db.child("SensorsData").order_by_key().limit_to_last(1).get()
items = latest_entry.each()
if items is not None:
    for item in items:
        data = item.val()
        latest_waterlevel = data.get("waterlevel")
        print("Latest Waterlevel:", latest_waterlevel)
else:
    print("No waterlevel data found.")

# Assume the maximum waterlevel is known, for example:
max_waterlevel = 28  # Change this value as needed

# Calculate the percentage
waterlevel_percentage = (latest_waterlevel / max_waterlevel) * 100
print(f"Waterlevel Percentage: {waterlevel_percentage:.2f}%")

db.child("waterlevel_percentage").push({
    "waterlevel_percentage": waterlevel_percentage,
    "timestamp": datetime.now().strftime("%Y-%m-%d_%H-%M")
})
print("Water level percentage and timestamp sent to Firebase.")

# Fetch the last two waterlevel entries with timestamps
# waterlevels = [26, 15]  # [previous, latest]
# timestamps = ["2025-06-12_04-34", "2025-06-12_10-26"]  # [previous, latest]
entries = db.child("SensorsData").order_by_key().limit_to_last(2).get()
waterlevels = []  # [previous, latest]
timestamps = []  # [previous, latest]

# Convert timestamps to datetime and sort both lists by timestamp descending (latest first)
fmt = "%Y-%m-%d_%H-%M"
combined = sorted(zip(timestamps, waterlevels), key=lambda x: datetime.strptime(x[0], fmt), reverse=True)
timestamps, waterlevels = zip(*combined)

t0 = datetime.strptime(timestamps[0], fmt)  # latest
t1 = datetime.strptime(timestamps[1], fmt)  # previous
delta_level = waterlevels[0] - waterlevels[1]
delta_time = (t0 - t1).total_seconds()
time_until_empty = None

# Water level should be decreasing (delta_level < 0)
if delta_level < 0 and delta_time > 0:
    rate = abs(delta_level) / delta_time  # positive rate
    time_until_empty = latest_waterlevel / rate
    print(f"Estimated time until next refill: {time_until_empty/3600:.2f} hours")

    # Send the estimated time until next refill and the latest timestamp to Firebase
    db.child("estimated_refill_time").push({
        "estimated_refill_time": time_until_empty,
        "timestamp": datetime.now().strftime("%Y-%m-%d_%H-%M")
    })
    print("Estimated refill time and timestamp sent to Firebase.")
else:
    print("No consumption detected or invalid data.")