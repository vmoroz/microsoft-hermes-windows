function resolveAfterTimeout() {
  return new Promise((resolve) => {
    setTimeout(() => {
      resolve("resolved");
    }, 0);
  });
}

resolveAfterTimeout();
  
//async function asyncCall() {
function asyncCall() {
  console.log("calling");
//  const result = await resolveAfterTimeout();
//  console.log(result);
  // Expected output: "resolved"
}

//asyncCall();