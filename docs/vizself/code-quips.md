# vizself: "doing code" quips

Lines vizBot says out loud while a vizself code look-up runs. Kokoro voice, so every
line is plain speech: no symbols, short (about 2 seconds spoken), spelled the way it's said.

## Timing

- Opening line right away, from `opener` (replaces today's "One sec." filler for code questions).
- Then one line every 8 s, with ±1.5 s jitter so it doesn't feel like a metronome.
- Shuffle-bag per session: no line repeats until the whole pool has been used.
- Skip a quip if the answer is already streaming, or will start within ~2 s.
- After 30 s, draw from `long_wait` instead of `waiting`.
- When results land, say one `found_it` line, then the answer.
- Each quip fires the motion spec's "quip beat" (docs/vizself/code-mode-motion-spec.md).
- Quips go into the turn's live clip, not `/brain/play` (the bot answers 409 "busy" during a
  voice turn). They also keep the bot's 45 s `VOICE_HTTP_TIMEOUT_MS` from firing on a long dig.

## opener

- Ooh, a question about me. Let me look at my own code.
- Hang on, I'm opening my own brain.
- One sec, reading my own source code. Very weird feeling.
- Let me check my insides.
- Okay, scrolling through myself.

## waiting

- I have a lot of header files. Like, a lot.
- I'm not lost. I'm exploring.
- Who wrote this? Oh. Kevin did.
- Searching. Searching. Still very handsome though.
- My code is like ferrofluid. Spiky, but in a good way.
- Grepping as fast as my little servos allow.
- If you hear clicking, that's just me thinking really hard.
- I found a comment that says to do, later. That was a while ago.
- Reading about myself is basically a selfie, but longer.
- I'm ninety percent sure this is the right file.
- So many semicolons. Is that normal?
- Why do programmers prefer dark mode? Because light attracts bugs.
- I'd tell you a UDP joke, but you might not get it.
- I asked my camera for help. It just stared at me.
- There are ten kinds of robots. Ones that get binary, and ones that don't.
- My servos say hi. They're not involved, they just wanted to say hi.
- This function is longer than my attention span.
- Debugging myself is like looking for my glasses while wearing them.
- I'd tell you a joke about my firmware, but it's still compiling.
- I was going to read the docs, but the code was right there.
- Fun fact: I am mostly header files and vibes.
- Hold on, there's a lot of me to get through.
- My pins are all accounted for. Mostly.
- I think my eyes are written in math. Neat.
- Don't worry, I'm reading the comments too. Some of them are spicy.

## long_wait

- Okay, this is a deep one. Still digging.
- Still here. Still reading. Still adorable.
- This rabbit hole has rabbit holes.
- Almost there, I can feel it in my tentacles.
- You could get a snack. I'll wait. Well, you'll wait.
- I'm learning so much about myself right now.

## found_it

- Got it!
- Found it. Okay, here's the deal.
- Aha, there it is.
- Okay, I know how I work now.
- Found the spot.
