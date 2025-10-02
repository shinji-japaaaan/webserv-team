Webサーバー概略図と解説
サーバー全体の処理フロー
           ┌───────────────┐
           │    main.cpp    │
           │ Server server  │
           │ server.init()  │
           │ server.run()   │
           └───────────────┘
                   │
         ┌─────────┴─────────┐
         │                   │
 ┌─────────────────────┐   ┌─────────────────────┐
 │ handleNewConnection() │ │ handleClient(i)      │
 │ - accept new client   │ │ - recv data          │
 │ - add to poll list    │ │ - echo back          │
 └─────────────────────┘   └─────────────────────┘

