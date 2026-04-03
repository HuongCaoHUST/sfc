from ultralytics import YOLO
model = YOLO("http://127.0.0.1:8000/yolo", task="detect")
cap = cv2.VideoCapture("./dashboard/test_video.mp4")
w, h, fps = (int(cap.get(x)) for x in (cv2.CAP_PROP_FRAME_WIDTH, cv2.CAP_PROP_FRAME_HEIGHT, cv2.CAP_PROP_FPS))

out = cv2.VideoWriter('output_result.mp4', cv2.VideoWriter_fourcc(*'mp4v'), fps, (w, h))
results = model.predict(source="input_video.mp4", stream=True)

for r in results:
    annotated_frame = r.plot() 
    out.write(annotated_frame)

    cv2.imshow("Triton YOLO Inference", annotated_frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
out.release()
cv2.destroyAllWindows()