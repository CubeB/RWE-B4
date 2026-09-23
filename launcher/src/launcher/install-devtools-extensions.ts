import installExtension, { REDUX_DEVTOOLS } from "electron-devtools-installer";

export function installExtensions(): Promise<any> {
  return installExtension(REDUX_DEVTOOLS)
    .then(extension => {
      console.log(`Installed ${extension.name}`);
    })
    .catch((err: string) => {
      console.log("An error occurred: ", err);
    });
}
