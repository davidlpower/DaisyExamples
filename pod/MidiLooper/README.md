**MidiLooperProBeta - Quick Usage Guide**

**Overview:**
MidiLooperProBeta is a dual-loop audio processing unit designed for real-time performance on the Electrosmith Daisy Pod. It enables recording, playback, overdubbing, real-time crossfading, and playback speed adjustments between two independent audio loops, alongside reverb and delay effects.

**Mode Selection (Encoder):**

* Rotate the encoder to cycle through modes: Reverb (REV), Delay (DEL), and Looping (LOOP).
* LEDs flash the color representing the currently selected mode while rotating:

  * REV mode: Blue
  * DEL mode: Green
  * LOOP mode: White
* If the encoder is stationary for 2 seconds, LEDs stop flashing, and the selected mode is set.

**Loop Operations (LOOP Mode):**

* **Button 1:** Toggles Loop A between playback and overdubbing.
* **Button 2:** Toggles Loop B between playback and overdubbing.
* **Hold Button 1 + Rotate Knob 1:** Adjust Loop A length.
* **Hold Button 2 + Rotate Knob 2:** Adjust Loop B length.

**Controls (without button presses):**

* **Knob 1:** Adjust playback speed for both loops simultaneously.
* **Knob 2:** Crossfade between Loop A (fully CCW) and Loop B (fully CW).

**MIDI Control (CC numbers):**

* **10:** Reverb dry/wet mix
* **11:** Feedback amount (delay/reverb)
* **12:** Delay time (in milliseconds)
* **13:** Mode selection increment/decrement
* **14:** Loop crossfade amount

**Indicators (LEDs):**

* Indicate current operating mode and loop state:

  * **REV mode:** Blue intensity represents dry/wet and feedback.
  * **DEL mode:** Green intensity represents delay and feedback.
  * **LOOP mode:** LED1 indicates Loop A state, LED2 indicates Loop B state:

    * **Idle:** Dim white
    * **Recording:** Red
    * **Playing:** Green
    * **Overdubbing:** Orange

**Real-time Effects:**

* **Reverb:** Adjustable via knob or MIDI, adding spaciousness to audio input.
* **Delay:** Adjustable delay time and feedback for echo effects.

**Operational Recommendations:**

* Adjust crossfade carefully for creative blending between loops.
* Monitor loop states through LED feedback for precise control.
* Integrate MIDI for advanced performance automation.

MidiLooperProBeta is optimized for low latency and real-time responsiveness, providing powerful, creative looping and processing capabilities for dynamic performances.
