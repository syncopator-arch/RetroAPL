# RetroAPL: How It Came About

*A bit of history on where RetroAPL came from and why it works the way it does.*

---

This is an attempt to record some history and some remembrances of what motivated and ultimately became RetroAPL. I guess I'll start at the beginning.

## First encounter with APL

Around 1972–1973 I started at a junior college with electronics as my major. One day in class we had to do impedance plots, usually on graph paper, which was kind of a slog. But somebody in the class said, "Oh, we can do that on a computer." And I said, "Oh yeah, how do you do that?" I'd never seen a computer before and didn't know much of anything about them. This was the early '70s and personal computers weren't a thing yet.

He said, "Well, we go over to the computer building." So we went over, and they had an IBM System/360 mainframe in a room with a raised floor and air conditioning. It was the whole shebang: an array of keypunch machines, card readers, line printers and a computer operator who fed Fortran jobs into the card reader. You wrote your program on coding forms and punched the cards yourself. That's how all work was done back then.

The hall around that room was all windows so you could see the computer. They wanted to show it off. Word had it that IBM had gifted them the computer, though that was just a rumor. It was connected to 80 APL terminals around the campus, including 40 in a room right across from the computer room. That room was also windowed and was set up like a classroom so an entire 40-person class could sit at their own APL terminals. The instructor could teach and the students could try things immediately.

## Typewriter terminals

In those days computers were not very interactive. You fed cards into a hopper, waited for the job to run and got a result on the line printer. There wasn't much turnaround. The early attempts at interactivity needed some kind of I/O device, and at the time there really wasn't one. Video terminals didn't exist yet because the technology hadn't progressed that far. Television existed, but the ability to generate real-time text on a screen wasn't there yet.

So most input devices of that era that were interactive at all were based on modified typewriters. The Friden Flexowriter went back as far as the '50s. It was a big, heavy machine full of solenoids to drive the typewriter mechanics. All that extra mechanism, needed so the computer could type on it as well as a person, made it much larger than a typewriter.

But IBM had an edge because they had a leading-edge typewriter at the time: the IBM Selectric. It used a type ball that reduced how far the keys had to throw to hit the paper, so it could run faster. All the symbols were right there on the type ball, which would rotate into position and strike the paper through a ribbon just like classic typewriters did. Being relatively fast was an advantage, and IBM decided to use it with their computers.

## Why APL looked the way it did

APL came about largely because it fit that scenario. The type balls were interchangeable. It was just a little plastic ball with all the symbols on the outside, and you could pop it off and put on one with a different font or different characters. So essentially what they did with APL was sacrifice the lowercase characters and use the APL symbols instead to make a computer language. See [README.md](README.md) for a photo of the original APL terminal.

The language was designed that way mainly because typing took a long time. A complex program with a lot of text would take forever to print. Printing out an entire Fortran program would take several minutes at best, so it wasn't very interactive. They needed a very terse language, and APL is what came out of that: single symbols that could do complex operations and be combined in various ways.

That's what they had in this classroom, and students could use it whenever there wasn't a class. You could use the demo login each terminal had or sign up for your own account, where your work would still be there the next day. With a demo login nothing lasted. You'd log out, somebody else would log in and it would be cleared out for them.

I was fascinated by this thing. Wow, what else can it do? We did our impedance plots by running a program in a public workspace from these terminals. And the terminals were readily available because there weren't that many classes held there. The room was often free for students to walk in and sit down at a terminal.

## The 2741 and overstrikes

The terminals were IBM 2741s. They were essentially Selectric typewriters built into a desk with a lot more mechanics so they could do everything they needed to do with the computer. They had a pedestal full of circuit cards that communicated with the computer and translated back and forth between the human and the machine.

You could write short programs in APL, and listing a famous APL one-liner would only take a few seconds. Baud rates on these things were not very high. By the end of the modem days we were talking about 19.2k or faster; the 2741 ran at 134.5 baud, about 15 characters per second. But because APL was so brief and concise and yet so capable, it fit the hardware availiable well. A lot of people think APL was ahead of its time. No, it was a product of its time. It's still around, but it's evolved a lot and they've added a lot to it.

The symbols worked well with the 2741 because of overstrikes. You made an overstrike symbol by typing a character, backspacing and typing another character on top of it. So you only had to learn where the basic symbols were on the keyboard, and there weren't that many of them. Maybe half of the symbols or fewer were on a key by themselves and the rest were combinations of two. You didn't have to learn a massive keyboard with a key for every symbol because overstrikes gave you more combinations from the same smaller set.

## From System/360 to System/370

So the first computer I ever saw was this System/360. A year later they upgraded to a System/370, which was kind of a surprise because it got smaller. You'd think a newer, faster system would be bigger, but they were learning how to make things smaller. It was faster and it supported the same APL terminals.

I learned APL, took some classes in it and learned to love it. There were a lot of hackers in those classes who loved APL too. At some point they found a couple of bugs in the system and were able to dump some of the binary code of the APL program. They did some reverse engineering and learned a lot about how APL worked inside: how workspaces were structured and how things were done. I learned a lot there too. It came back to me over time and helped when I decided to develop my own.

## After school

After I left school I didn't get to see APL much. Personal computers started coming in, and I got my first one around 1976: an IMSAI 8080 that I soldered together from a kit myself. Somebody had produced a free tiny APL that was vector-only and ran on it, and I remember playing with that.

I worked for Tektronix for about four years fixing oscilloscopes, and I bought and built my IMSAI while I was there. Tektronix was a great place to work, but I wanted to get more into software and they weren't doing that, at least not in the field offices I was working in. So I found another job, learned to code in C and spent the next 40-some years as a software developer. I only retired in 2023. I always enjoyed software and I still do.

## Building my own APL

At some point in the 1980s, after I'd learned C, I decided to put my knowledge of APL to use because I missed it. I hadn't seen any real APL in years. There were some PC packages, and IBM released an APL for the IBM PC at some point. But they weren't cheap and didn't excite me enough to buy. Instead I took a C compiler and started working on my own.

I put together an initial APL interpreter using what I knew about workspace structures and how APL worked, which was quite a bit, and got it going. It was far from complete and had bugs, but it did a lot of things I could have fun with. I used it as a desk calculator and expanded it a little over time. One thing my version had that the others didn't seem to have as much was interfacing with the outside world: running other programs and collecting their results, and graphics.

I can't remember how I handled the characters at first, but I ended up using Delphi to write the front end, and I found a freely available APL TrueType font. It's the one I'm still using in RetroAPL because it's modeled after the 2741 typeface and looks just like a 2741. It has all the overstrike symbols and it's worked fine. The program ran only on Windows because that's what Delphi ran on and that's the computer I had to work with.

Then I got into Linux. I started with Slackware in the early '90s I think, and I've been a Linux follower ever since. But the few times I tried to port my APL to Linux the character font was a problem. I didn't want to have to change the console font and didn't see a good way around it, because Linux was shell-oriented and I was working in the shell. In the early days of Linux, robust windowed desktops didn't much exist. Figuring out how to toggle to an APL font cleanly was a puzzle I never solved in a way I liked. With Delphi I could put up a window of its own and do whatever I wanted with the font, so that made it workable.

## How functions work in RetroAPL

I made some concessions. The way APL works, functions actually get compiled into the workspace. If I remember right it's reverse Polish, or at least a parenthesis-free form, because that's easier and more efficient to process. But it means that to list out a function in the workspace you essentially have to decompile it.

I didn't want to write a whole decompiler. And line-by-line function editing didn't make a whole lot of sense once video screens were in use. So I built in a visual editor that saves each function as a text file on disk. Then a system command I call `)ENTER` compiles it into the workspace.

That's how functions get created and why RetroAPL works the way it does. It doesn't use line-oriented function editing. You edit visually, it saves to disk and then you enter it, or the editor does that for you automatically. Then you can run it from the workspace.

So this RetroAPL isn't quite trying to be 100% emulation of every aspect of the classic early days of working wiht APL.  It reproduces many of the features I remember and value while also embracing some more modern conveniences as well as practical characteristics given more modern hardware.

## Extensions: music and graphics

I lived with it in that limited form for quite a while. It didn't do a lot of things but it did the basics, so it was enough to have fun with. I added some extensions so I could read and write files and execute external programs. That let me play around with MIDI sequences. I could create a music sequence in a couple of array variables and have it write them out to a file. A C program would then pick that up and convert it to a MIDI file I could play back in a MIDI sequencer. So I did some composing and playing around that way. It was very useful for exploring the Schillinger technique of music composition because APL fits right in with it in some ways. It's an interesting thing to explore if you're into that.

I also added some graphics features that could open a graphics window and plot and draw and do a few things. So I'd implemented all of that, but for years I didn't get any further. I just used it on my Windows machines for quick calculations or a quick program to do something.

## Retirement, and bringing in Claude

Once I retired I started to think about it a little more and play with it a little more. And then AI came along. I retired right about when AI started completely transforming the computer industry and software development. So I thought, let's see what happens if I give this thing to Claude and tell it to make some enhancements, fix some bugs or make an adjustment.

I learned fairly quickly how to keep Claude on track, which is the most important thing. You have to write a good plan up front and be really clear about what you want. If you leave anything to chance, that's what you'll get: random chance. Also don't let a context go on too long. It gets cluttered with stuff that's not important to the business at hand, and that's when you get hallucinations. So when things have gotten complex enough, start a new chat with a fresh context. I keep a few files prepared with the basics I want in every chat, upload them and say, OK, now I want to focus on this different aspect. That came together pretty well, and I was ready to go great guns on further enhancing and fleshing out the program.

## Porting to 64-bit Linux

That included completely reworking it for Linux, because I use Linux more than Windows these days. I work with Ubuntu, and I missed having my little calculator APL there. But it was not easy to build on Linux. The Linux I was running was a 64-bit OS. My code would run on 64-bit Windows, but there it actually runs as a 32-bit program. That isn't what happens when you build on Linux. You get a 64-bit program, so pointers are 64 bits. And there are a lot of stored pointers in APL workspaces. Maybe some versions don't do that as much, but mine did, as did the original it was based on.

So it turned out to be major surgery to convert it from stored pointers to offsets from a workspace base pointer. I kept 32-bit offsets so it can still address as much RAM as before, which is way more than you'd probably ever need with APL. That way it builds on a 64-bit machine but behaves the same as the 32-bit version. The workspaces didn't get bigger and less had to change. It took at least a couple of weeks of daily plugging away, getting it to do this and that and addressing the different aspects. But Claude ended up doing a pretty good job.

## What AI is good at

Once it was running I started to wonder what else I could get this code to do, and I started adding features. I had a wish list of things I would have liked to add. But I've been developing software for 45 years or so, and developing software takes a lot of focus, especially when it's complex. This was getting pretty complex. I'm retired and I'm getting old. My brain still works, but I don't want to concentrate that deeply on that stuff anymore. I did that for a long time.

That's what AI is good at. Some people may not like AI or doing things with AI, but an AI can write code way faster than any human can. That's one thing I've learned from Claude. It can write code so much faster than I ever could, or than anybody I've ever heard of could. It can do things in a couple of minutes that would take a human programmer hours. That's one reason software is becoming more and more an AI thing. But it does take guidance, quite a bit of it, and I learned how to give that guidance, at least as things were at the time I did this. Things are evolving quickly in AI and maybe there's less need for as much guidance now. That remains to be seen.

## Qt 6 and portability

I was able to bring my old APL back to life using Qt 6 for the windowing instead of Delphi. That gave me a more portable version that runs on Linux and Windows. It could probably be built on a Mac without too much trouble because Qt 6 works on Mac as well. I don't have a Mac so I'm not doing any of that, but if somebody wants to I'd be interested to hear about it. I'd think it would be pretty straightforward because it's designed to be portable.

The front-end window keeps a bit of the 2741 spirit. On the original everything was on paper, an endless roll or fanfold that kept feeding through the platen as you worked. So the Qt window scrolls until you clear the screen. There are a couple of key codes that will clear the screen or home the cursor when sent to it. Documentation for that and other details like it isn't there yet, but it's getting there.

## What it doesn't do

I haven't followed the evolution of APL much since those early days, and I didn't really encounter it at work. Frankly I think it may have gotten too complex anyway. RetroAPL doesn't really support nested arrays. There is a `⎕STRUCT` system function I added as an experiment that behaves a bit like them. But the glyphs and features that go with nested arrays aren't there, such as enclose and disclose. I suppose they could be added. `⎕STRUCT` hasn't had a lot of testing and I suspect it may be kind of buggy. I haven't been motivated to dig into it further. At least not yet.

## Learning it

To learn RetroAPL's flavor of APL, a good resource is one of the classic texts, *APL: An Interactive Approach* by Gilman and Rose. The first edition is what I learned on. Later editions are good too, though they cover APL2 features like nested arrays that RetroAPL doesn't support. Then again, if you're looking to extend the codebase with those features they could come in handy.

## Open source

There are probably other things I could say about the program, but this is really more about the history. I've always had a soft spot in my heart for APL. It was the first language I ever learned, on the first computer I ever encountered, and it's been fun to revisit it in my retirement years.

So I'm open-sourcing it. By the time you're reading this it's probably already out there. Have fun with it if you feel like it, and maybe get some sense of what it was like on the original 2741. And maybe actually find it useful.
