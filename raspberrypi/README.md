### Process Overview for Camera.py

![Upload Firebase Flowchart](assets/Upload%20Firebase%20Flowchart.png)

1. **Start & Timer Check**  
    - The process begins by checking if 30 minutes have passed.  
    - If **NO**, it loops back and waits.  
    - If **YES**, it proceeds to capture new photos.

2. **Photo Capture & Upload**  
    - Capture photos using the camera.  
    - Save the photos locally on the device.  
    - Upload the photos to Firebase Cloud Storage.

3. **Download for Processing**  
    - Download the uploaded photos from Firebase for further analysis.

4. **Image Analysis**  
    - **Greyscale Conversion**: Convert the downloaded image to greyscale.  
    - **Bounding Box Detection**: Detect and mark leaves and brown spots.  
    - **Load Trained Model**: Load a pre-trained machine learning model.  
    - **Prediction**: Use the model to classify the leaf’s health or disease.

5. **Labeling & Upload**  
    - Annotate the image with predictions and detected spots.  
    - Save the labeled photos locally.  
    - Upload the labeled images back to Firebase.

6. **End**  
    - The process stops and waits for the next 30-minute interval to restart.

### Process Overview for WaterTank.py

    ![Water Tank Detection Flowchart](assets/Water%20Tank%20Detection%20Flowchart.png)

    1. **Start & Sensor Check**  
        - The process begins by checking the water level sensor data.  
        - If the water level is **sufficient**, it loops back and waits.  
        - If the water level is **low**, it proceeds to the next step.

    2. **Notification & Logging**  
        - Send a notification to the user about the low water level.  
        - Log the water level data locally for tracking purposes.

    3. **Pump Activation**  
        - Activate the water pump to refill the tank.  
        - Monitor the sensor data to ensure the tank is refilled properly.

    4. **End**  
        - The process stops and waits for the next sensor check interval.