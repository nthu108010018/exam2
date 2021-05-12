# exam2
feature為判定晃動幅度
若大於一定數值feature則為1



用HW3code改
在連線以後 於screen打入/capture/run則可進行
同時開啟plot_index.py 

在晃動mbed之後code將會記錄三軸加速度 給tflite做判斷gesture
用判別式判定feature

一做完便進行publish
plot_index.py收到5個以後會畫出圖片
x軸為sequencenumber y 為index_number
時間不足未畫出第二個圖片
