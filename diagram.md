graph LR
    %% SENDER CONTAINER
    subgraph SENDER [Container: video_source]
        A[v4l2src /dev/video0] --> B[videoconvert]
        B --> C[x264enc]
        C --> D[rtph264pay]
        D --> E{tee: t}
        E --> |Queue 1| F[udpsink port:5000]
        E --> |Queue 2| G[udpsink port:5001]
    end

    %% NETWORK FLOWS
    F -.-> |video_net:5000| H
    G -.-> |video_net:5001| O
    P -.-> |video_net:5000| Q

    %% FLIPPER CONTAINER
    subgraph FLIPPER [Container: ai_inference]
        H[udpsrc] --> I[rtph264depay]
        I --> J[avdec_h264]
        J --> K[videoconvert BGR]
        K --> L[YOLOv11_element]
        L --> M[videoconvert]
        M --> N[x264enc / pay]
        N --> P[udpsink host:receiver]
    end

    %% RECEIVER CONTAINER
    subgraph RECEIVER [Container: video_compositor]
        %% Luồng từ Flipper (YOLO)
        Q[udpsrc port:5000] --> R[depay / decode]
        R --> S[videoscale]
        S --> |Processed| COMP1[sink_1: Top-Right]

        %% Luồng từ Sender (Raw)
        O[udpsrc port:5001] --> P1[depay / decode]
        P1 --> Q1[videoscale]
        Q1 --> R1{tee: t_raw}
        
        R1 --> |Branch 1| COMP0[sink_0: Top-Left]
        R1 --> |Branch 2| COMP2[sink_2: Bottom-Left]
        R1 --> |Branch 3| COMP3[sink_3: Bottom-Right]

        %% Compositor Output
        COMP0 & COMP1 & COMP2 & COMP3 --> Z[compositor]
        Z --> AA[videoconvert]
        AA --> AB[autovideosink]
    end