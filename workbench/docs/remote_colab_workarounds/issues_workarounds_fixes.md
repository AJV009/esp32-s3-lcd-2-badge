# Known Issues and Workarounds · googlecolab/colab-vscode Wiki

# Known Issues and Workarounds

[Jump to bottom](#wiki-pages-box)

Evan Wiederspan edited this page Nov 15, 2025 · [4 revisions](https://github.com/googlecolab/colab-vscode/wiki/Known-Issues-and-Workarounds/_history)

Our initial release has gone out with a handful of known issues. This page centralizes these known gaps and recommends workarounds.

# Unsupported `google.colab` functionality

[](#unsupported-googlecolab-functionality)

There are some `google.colab` libraries and features that do not currently work in VS Code due to their reliance on the Colab Web UI. These are listed below, alongside any known workarounds until we're able to fix them.

## `auth.authenticate_user()`

[](#authauthenticate_user)

This currently shows the auth URL in the _QuickPick_ menu at the top of the screen, making it impossible to click on the URL.

The workaround is to use the relevant [Python Cloud Client Library](https://docs.cloud.google.com/python/docs/reference).

## `drive.mount()`

[](#drivemount)

There's no way to mount Google Drive from VS Code today. You can interact with your Drive contents through the [Drive Python API](https://developers.google.com/workspace/drive/api/quickstart/python).

## `files.download()`

[](#filesdownload)

You can use a widget to download a file from your runtime, see [this notebook](https://colab.research.google.com/drive/1VXiCVNqE_9bUHnUDIDAt-wEOd_VmlFVq) for an example.

## `files.upload()` and `files.upload_file()`

[](#filesupload-and-filesupload_file)

The [File Upload](https://ipywidgets.readthedocs.io/en/8.1.3/examples/Widget%20List.html#file-upload) IPyWidget can be used to upload your files to the kernel.

## `sheets.InteractiveSheet()`

[](#sheetsinteractivesheet)

The Google Sheets module relies on `auth.authenticate_user`. See above. Due to this, there is no known workaround.

## `userdata.get()`

[](#userdataget)

`userdata.get` currently returns an error. The only current workaround is to go to the Colab Web UI and copy the secret value there.

To make the secret value available to your code without putting it into the actual notebook, you can use an ipywidget text input to store the value in an environment variable, like the following:

import ipywidgets as widgets
from IPython.display import display, clear\_output
import os

def input\_secret(secret\_name):
  def handle\_submit(sender):
      user\_value \= sender.value
      os.environ\[secret\_name\] \= user\_value
      print(f"Environment variable {secret\_name} set")
      with output\_area:
          clear\_output()

  \# Create a text input widget
  text\_input \= widgets.Text(
      value\='',
      placeholder\=f'Value for {secret\_name}',
      description\=f'Secret Value',
      disabled\=False
  )

  \# Create an output widget to control clearing
  output\_area \= widgets.Output()

  \# Link the submit event to the handler function
  text\_input.on\_submit(handle\_submit)

  \# Display the widget inside the output area, then display the output area
  with output\_area:
      display(text\_input)
  display(output\_area)

input\_secret("GEMINI\_API\_KEY")

### Clone this wiki locally
