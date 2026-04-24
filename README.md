# V4-Ereader
Not really a programmer but thought I'd have some fun using Gemini AI to get a spare Heltec V4 board to be an e-reader.

This likely works with the v3 version of the Heltec board however I have not tested it.
I 3d printed the "Pocket Pager case" from Alley Cat @AlleyCat from Printables.com (https://www.printables.com/model/1519914-heltec-lora-32-v4v3-pocket-pager-case) 

Navigation on the device:
2 buttons on the device 1 is the reset button the other is what is used for navigation. 
-single click advances 1 page
-long hold select or exit from reading mode
-double click goes up or back a page in reading mode.

To load the device with ebooks you'll need your ebook in the .txt format.
Go to: settings ->Book Upload

This will trigger an open SSID to be created "EsBook32_Portal" once connected go to 192.168.4.1 where a gui will show where you can Set the Author and Book Title and select the .txt to upload. Once confirmation of successful upload occurs in the browser you can double click to turn off the SSID and your book will now show in the main menu. Each author will have its on "folder" on the main screen. Files are all stored in the same folder on the device itself.